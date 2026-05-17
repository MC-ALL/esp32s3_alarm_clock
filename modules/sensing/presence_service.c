#include "app_module.h"
#include <app/hw_config.h>
#include <app/presence_service.h>
#include <app/module_common.h>

#include <inttypes.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

static const char *TAG = "presence";
static QueueHandle_t s_uart_event_queue;
static TaskHandle_t s_presence_task;
static bool s_present_hint;
static uint32_t s_rx_bytes;

static void presence_task(void *arg)
{
	(void)arg;

	uint8_t rx_buf[128];
	uart_event_t event;
	bool last_hint = s_present_hint;

	for (;;) {
		if (xQueueReceive(s_uart_event_queue, &event, pdMS_TO_TICKS(100)) == pdTRUE) {
			if (event.type == UART_DATA && event.size > 0) {
				int read = uart_read_bytes(UART_NUM_1, rx_buf,
					sizeof(rx_buf) < (size_t)event.size ? sizeof(rx_buf) : (size_t)event.size,
					0);
				if (read > 0) {
					s_rx_bytes += (uint32_t)read;
					ESP_LOGI(TAG, "uart rx bytes=%d total=%" PRIu32, read, s_rx_bytes);
				}
			}
		}

		const bool current_hint = gpio_get_level(APP_PIN_LD2410_OUT) != 0;
		if (current_hint != last_hint) {
			last_hint = current_hint;
			s_present_hint = current_hint;
			ESP_LOGI(TAG, "OUT hint changed: %s", current_hint ? "present" : "absent");
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
		return (int)err;
	}

	err = uart_set_pin(UART_NUM_1, APP_PIN_LD2410_UART_TX, APP_PIN_LD2410_UART_RX,
		UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	s_present_hint = gpio_get_level(APP_PIN_LD2410_OUT) != 0;
	s_rx_bytes = 0;
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

	(void)uart_driver_delete(UART_NUM_1);
	return 0;
}

bool presence_service_is_present_hint(void)
{
	return s_present_hint;
}

uint32_t presence_service_get_rx_bytes(void)
{
	return s_rx_bytes;
}
