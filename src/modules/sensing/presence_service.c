#include "app_module.h"
#include <core/hw_config.h>
#include <sensing/presence_ld2410.h>
#include <sensing/presence_service.h>
#include <sensing/presence_status_runtime.h>
#include <sensing/presence_uart.h>
#include <core/module_common.h>

#include <inttypes.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

static const char *TAG = "presence";
static QueueHandle_t s_uart_event_queue;
static TaskHandle_t s_presence_task;
static int64_t s_last_uart_rx_us;
static int64_t s_last_uart_log_us;
static uint32_t s_uart_rx_bytes;
static presence_ld2410_parser_t s_ld2410_parser;
static presence_status_runtime_t s_status_runtime;

static void presence_refresh_derived_status(int64_t now_us)
{
	const bool out_hint = gpio_get_level(APP_PIN_LD2410_OUT) != 0;
	presence_status_runtime_refresh(&s_status_runtime, out_hint, s_uart_rx_bytes, now_us);
}

static void presence_apply_ld2410_frame(const presence_ld2410_frame_t *frame, void *ctx)
{
	(void)ctx;
	if (frame == NULL) {
		return;
	}

	const int64_t now_us = esp_timer_get_time();
	presence_status_runtime_apply_frame(&s_status_runtime, frame, now_us);
	presence_refresh_derived_status(now_us);
}

static void presence_feed_bytes(const uint8_t *data, size_t len, void *ctx)
{
	(void)ctx;
	if (data == NULL || len == 0U) {
		return;
	}

	if (!presence_ld2410_parser_feed(&s_ld2410_parser, data, len, presence_apply_ld2410_frame, NULL)) {
		ESP_LOGW(TAG, "rx frame buffer overflow, reset parser");
	}
}

static void presence_task(void *arg)
{
	(void)arg;

	uart_event_t event;
	bool last_detected = presence_status_runtime_present_hint(&s_status_runtime);

	for (;;) {
		if (xQueueReceive(s_uart_event_queue, &event, pdMS_TO_TICKS(100)) == pdTRUE) {
			if (event.type == UART_DATA && event.size > 0) {
				presence_uart_drain((size_t)event.size, presence_feed_bytes, NULL,
						    &s_uart_rx_bytes, &s_last_uart_rx_us);
			} else if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL) {
				ESP_LOGW(TAG, "uart overflow, flushing input");
				presence_uart_flush_input(s_uart_event_queue);
				presence_ld2410_parser_reset(&s_ld2410_parser);
			}
		}

		const int64_t now_us = esp_timer_get_time();
		presence_refresh_derived_status(now_us);

		app_presence_status_t snapshot = { 0 };
		(void)presence_service_get_status(&snapshot);
		if (s_last_uart_log_us == 0) {
			s_last_uart_log_us = now_us;
		} else if ((now_us - s_last_uart_log_us) >= 1000000) {
			const bool uart_active = presence_uart_active_recently(s_last_uart_rx_us, now_us);
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
	int ret = presence_uart_init(&s_uart_event_queue);
	if (ret != 0) {
		return ret;
	}

	const bool out_pin_present = gpio_get_level(APP_PIN_LD2410_OUT) != 0;
	s_last_uart_rx_us = 0;
	s_last_uart_log_us = 0;
	s_uart_rx_bytes = 0;
	presence_ld2410_parser_reset(&s_ld2410_parser);
	presence_status_runtime_init(&s_status_runtime, out_pin_present);
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

	presence_ld2410_parser_reset(&s_ld2410_parser);
	presence_uart_deinit();
	return 0;
}

bool presence_service_is_present_hint(void)
{
	return presence_status_runtime_present_hint(&s_status_runtime);
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
	return presence_status_runtime_get(&s_status_runtime, out_status);
}
