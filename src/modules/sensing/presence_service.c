#include "app_module.h"
#include <hw_config.h>
#include <presence_service.h>
#include <module_common.h>

#include <inttypes.h>
#include <string.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

static const char *TAG = "presence";
static QueueHandle_t s_uart_event_queue;
static TaskHandle_t s_presence_task;
static bool s_present_hint;
static int64_t s_last_uart_rx_us;
static int64_t s_last_uart_log_us;
static uint32_t s_uart_rx_bytes;
static uint8_t s_rx_frame_buf[256];
static size_t s_rx_frame_len;
static app_presence_status_t s_status;
static portMUX_TYPE s_presence_lock = portMUX_INITIALIZER_UNLOCKED;

static bool presence_uart_active_recently(int64_t now_us)
{
	return (s_last_uart_rx_us > 0) && ((now_us - s_last_uart_rx_us) < 1000000);
}

static uint16_t presence_read_le16(const uint8_t *data)
{
	return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static void presence_refresh_derived_status(int64_t now_us)
{
	const bool out_hint = gpio_get_level(APP_PIN_LD2410_OUT) != 0;

	taskENTER_CRITICAL(&s_presence_lock);
	s_status.out_pin_present = out_hint;
	s_status.rx_bytes = s_uart_rx_bytes;
	s_status.updated_at_us = now_us;

	const bool frame_recent = s_status.frame_valid && ((now_us - s_status.last_frame_us) < 1500000);
	s_status.radar_healthy = frame_recent;
	s_status.using_out_fallback = !frame_recent;
	s_status.detected = frame_recent ? (s_status.target_state != 0U) : out_hint;
	s_present_hint = s_status.detected;
	taskEXIT_CRITICAL(&s_presence_lock);
}

static bool presence_parse_payload(const uint8_t *payload, size_t payload_len)
{
	if (payload == NULL || payload_len < 13U) {
		return false;
	}
	if ((payload[0] != 0x01U && payload[0] != 0x02U) || payload[1] != 0xAAU) {
		return false;
	}
	if (payload[payload_len - 2U] != 0x55U || payload[payload_len - 1U] != 0x00U) {
		return false;
	}

	const int64_t now_us = esp_timer_get_time();
	const uint8_t target_state = payload[2];
	const uint16_t moving_distance_cm = presence_read_le16(&payload[3]);
	const uint8_t moving_energy = payload[5];
	const uint16_t stationary_distance_cm = presence_read_le16(&payload[6]);
	const uint8_t stationary_energy = payload[8];
	const uint16_t detection_distance_cm = presence_read_le16(&payload[9]);
	uint8_t max_moving_gate = 0;
	uint8_t max_stationary_gate = 0;

	if (payload[0] == 0x01U && payload_len >= 35U) {
		max_moving_gate = payload[11];
		max_stationary_gate = payload[12];
	}

	taskENTER_CRITICAL(&s_presence_lock);
	s_status.frame_valid = true;
	s_status.target_state = target_state;
	s_status.moving_distance_cm = moving_distance_cm;
	s_status.moving_energy = moving_energy;
	s_status.stationary_distance_cm = stationary_distance_cm;
	s_status.stationary_energy = stationary_energy;
	s_status.detection_distance_cm = detection_distance_cm;
	s_status.max_moving_gate = max_moving_gate;
	s_status.max_stationary_gate = max_stationary_gate;
	s_status.last_frame_us = now_us;
	taskEXIT_CRITICAL(&s_presence_lock);

	presence_refresh_derived_status(now_us);
	return true;
}

static void presence_process_rx_buffer(void)
{
	static const uint8_t FRAME_HEADER[4] = { 0xF4, 0xF3, 0xF2, 0xF1 };
	static const uint8_t FRAME_TAIL[4] = { 0xF8, 0xF7, 0xF6, 0xF5 };

	for (;;) {
		if (s_rx_frame_len < 6U) {
			return;
		}

		size_t start = 0;
		while ((start + sizeof(FRAME_HEADER)) <= s_rx_frame_len) {
			if (memcmp(&s_rx_frame_buf[start], FRAME_HEADER, sizeof(FRAME_HEADER)) == 0) {
				break;
			}
			start++;
		}

		if (start > 0U) {
			if (start >= s_rx_frame_len) {
				s_rx_frame_len = 0;
				return;
			}
			memmove(s_rx_frame_buf, &s_rx_frame_buf[start], s_rx_frame_len - start);
			s_rx_frame_len -= start;
			if (s_rx_frame_len < 6U) {
				return;
			}
		}

		const uint16_t payload_len = presence_read_le16(&s_rx_frame_buf[4]);
		if (payload_len < 13U || payload_len > 64U) {
			memmove(s_rx_frame_buf, &s_rx_frame_buf[1], s_rx_frame_len - 1U);
			s_rx_frame_len--;
			continue;
		}

		const size_t total_len = 6U + (size_t)payload_len + sizeof(FRAME_TAIL);
		if (s_rx_frame_len < total_len) {
			return;
		}

		if (memcmp(&s_rx_frame_buf[6U + payload_len], FRAME_TAIL, sizeof(FRAME_TAIL)) != 0) {
			memmove(s_rx_frame_buf, &s_rx_frame_buf[1], s_rx_frame_len - 1U);
			s_rx_frame_len--;
			continue;
		}

		(void)presence_parse_payload(&s_rx_frame_buf[6], payload_len);
		if (s_rx_frame_len > total_len) {
			memmove(s_rx_frame_buf, &s_rx_frame_buf[total_len], s_rx_frame_len - total_len);
		}
		s_rx_frame_len -= total_len;
	}
}

static void presence_feed_bytes(const uint8_t *data, size_t len)
{
	if (data == NULL || len == 0U) {
		return;
	}

	if ((s_rx_frame_len + len) > sizeof(s_rx_frame_buf)) {
		ESP_LOGW(TAG, "rx frame buffer overflow, reset parser");
		s_rx_frame_len = 0;
	}

	if (len > sizeof(s_rx_frame_buf)) {
		data += (len - sizeof(s_rx_frame_buf));
		len = sizeof(s_rx_frame_buf);
	}

	memcpy(&s_rx_frame_buf[s_rx_frame_len], data, len);
	s_rx_frame_len += len;
	presence_process_rx_buffer();
}

static void presence_drain_uart(size_t bytes_pending)
{
	uint8_t rx_buf[128];
	size_t remaining = bytes_pending;

	while (remaining > 0U) {
		const size_t chunk = remaining > sizeof(rx_buf) ? sizeof(rx_buf) : remaining;
		int read = uart_read_bytes(UART_NUM_1, rx_buf, chunk, 0);
		if (read <= 0) {
			break;
		}

		s_uart_rx_bytes += (uint32_t)read;
		s_last_uart_rx_us = esp_timer_get_time();
		presence_feed_bytes(rx_buf, (size_t)read);
		remaining -= (size_t)read;
	}

	while (remaining == 0U) {
		size_t buffered = 0;
		if (uart_get_buffered_data_len(UART_NUM_1, &buffered) != ESP_OK || buffered == 0U) {
			break;
		}

		const size_t chunk = buffered > sizeof(rx_buf) ? sizeof(rx_buf) : buffered;
		int read = uart_read_bytes(UART_NUM_1, rx_buf, chunk, 0);
		if (read <= 0) {
			break;
		}

		s_uart_rx_bytes += (uint32_t)read;
		s_last_uart_rx_us = esp_timer_get_time();
		presence_feed_bytes(rx_buf, (size_t)read);
	}
}

static void presence_task(void *arg)
{
	(void)arg;

	uart_event_t event;
	bool last_detected = s_present_hint;

	for (;;) {
		if (xQueueReceive(s_uart_event_queue, &event, pdMS_TO_TICKS(100)) == pdTRUE) {
			if (event.type == UART_DATA && event.size > 0) {
				presence_drain_uart((size_t)event.size);
			} else if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL) {
				ESP_LOGW(TAG, "uart overflow, flushing input");
				(void)uart_flush_input(UART_NUM_1);
				xQueueReset(s_uart_event_queue);
				s_rx_frame_len = 0;
			}
		}

		const int64_t now_us = esp_timer_get_time();
		presence_refresh_derived_status(now_us);

		app_presence_status_t snapshot = { 0 };
		(void)presence_service_get_status(&snapshot);
		if (s_last_uart_log_us == 0) {
			s_last_uart_log_us = now_us;
		} else if ((now_us - s_last_uart_log_us) >= 1000000) {
			const bool uart_active = presence_uart_active_recently(now_us);
			const int64_t recent_rx_ms = s_last_uart_rx_us > 0 ? ((now_us - s_last_uart_rx_us) / 1000) : -1;

			ESP_LOGI(TAG,
				"detected=%d healthy=%d frame=%d fallback=%d out=%d state=%u md=%ucm me=%u sd=%ucm se=%u dd=%ucm uart=%d rx_ms=%" PRIi64 " bytes=%" PRIu32,
				snapshot.detected ? 1 : 0,
				snapshot.radar_healthy ? 1 : 0,
				snapshot.frame_valid ? 1 : 0,
				snapshot.using_out_fallback ? 1 : 0,
				snapshot.out_pin_present ? 1 : 0,
				(unsigned)snapshot.target_state,
				(unsigned)snapshot.moving_distance_cm,
				(unsigned)snapshot.moving_energy,
				(unsigned)snapshot.stationary_distance_cm,
				(unsigned)snapshot.stationary_energy,
				(unsigned)snapshot.detection_distance_cm,
				uart_active ? 1 : 0,
				recent_rx_ms,
				snapshot.rx_bytes);
			s_last_uart_log_us = now_us;
		}

		if (snapshot.detected != last_detected) {
			last_detected = snapshot.detected;
			ESP_LOGI(TAG, "presence changed: %s (%s)",
				snapshot.detected ? "present" : "absent",
				snapshot.using_out_fallback ? "out-fallback" : "frame");
		}
	}
}

int presence_service_init(void)
{
	const uart_config_t uart_cfg = {
		.baud_rate = APP_LD2410_UART_BAUDRATE,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		.source_clk = UART_SCLK_DEFAULT,
	};
	const gpio_config_t out_cfg = {
		.intr_type = GPIO_INTR_DISABLE,
		.mode = GPIO_MODE_INPUT,
		.pin_bit_mask = (1ULL << APP_PIN_LD2410_OUT),
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.pull_up_en = GPIO_PULLUP_DISABLE,
	};

	esp_err_t err = gpio_config(&out_cfg);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	err = uart_driver_install(UART_NUM_1, 2048, 0, 16, &s_uart_event_queue, 0);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	err = uart_param_config(UART_NUM_1, &uart_cfg);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
		(void)uart_driver_delete(UART_NUM_1);
		s_uart_event_queue = NULL;
		return (int)err;
	}

	err = uart_set_pin(UART_NUM_1, APP_PIN_LD2410_UART_TX, APP_PIN_LD2410_UART_RX,
		UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
		(void)uart_driver_delete(UART_NUM_1);
		s_uart_event_queue = NULL;
		return (int)err;
	}

	s_present_hint = gpio_get_level(APP_PIN_LD2410_OUT) != 0;
	s_last_uart_rx_us = 0;
	s_last_uart_log_us = 0;
	s_uart_rx_bytes = 0;
	s_rx_frame_len = 0;
	s_status = (app_presence_status_t){
		.detected = s_present_hint,
		.out_pin_present = s_present_hint,
		.using_out_fallback = true,
	};
	ESP_LOGI(TAG, "init uart=%d rx=%d tx=%d out=%d baud=%d",
		UART_NUM_1, APP_PIN_LD2410_UART_RX, APP_PIN_LD2410_UART_TX,
		APP_PIN_LD2410_OUT, APP_LD2410_UART_BAUDRATE);
	return 0;
}

int presence_service_start(void)
{
	BaseType_t ok = xTaskCreate(presence_task, "presence_task", 4096, NULL, 9, &s_presence_task);
	if (ok != pdPASS) {
		ESP_LOGE(TAG, "failed to create presence task");
		return -1;
	}

	return 0;
}

int presence_service_stop(void)
{
	if (s_presence_task != NULL) {
		vTaskDelete(s_presence_task);
		s_presence_task = NULL;
	}

	if (s_uart_event_queue != NULL) {
		s_uart_event_queue = NULL;
	}

	s_rx_frame_len = 0;
	(void)uart_driver_delete(UART_NUM_1);
	return 0;
}

bool presence_service_is_present_hint(void)
{
	return s_present_hint;
}

uint32_t presence_service_get_rx_bytes(void)
{
	return s_uart_rx_bytes;
}

bool presence_service_get_status(app_presence_status_t *out_status)
{
	if (out_status == NULL) {
		return false;
	}

	const int64_t now_us = esp_timer_get_time();
	presence_refresh_derived_status(now_us);

	taskENTER_CRITICAL(&s_presence_lock);
	*out_status = s_status;
	taskEXIT_CRITICAL(&s_presence_lock);
	return true;
}
