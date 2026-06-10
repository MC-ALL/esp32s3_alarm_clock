#include <sensing/presence_uart.h>

#include <core/hw_config.h>

#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>

static const char *TAG = "presence_uart";

int presence_uart_init(QueueHandle_t *event_queue)
{
	if (event_queue == NULL) {
		return (int)ESP_ERR_INVALID_ARG;
	}

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

	err = uart_driver_install(UART_NUM_1, 2048, 0, 16, event_queue, 0);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	err = uart_param_config(UART_NUM_1, &uart_cfg);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
		presence_uart_deinit();
		*event_queue = NULL;
		return (int)err;
	}

	err = uart_set_pin(UART_NUM_1, APP_PIN_LD2410_UART_TX, APP_PIN_LD2410_UART_RX,
			   UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
		presence_uart_deinit();
		*event_queue = NULL;
		return (int)err;
	}

	return 0;
}

void presence_uart_deinit(void)
{
	(void)uart_driver_delete(UART_NUM_1);
}

void presence_uart_flush_input(QueueHandle_t event_queue)
{
	(void)uart_flush_input(UART_NUM_1);
	if (event_queue != NULL) {
		xQueueReset(event_queue);
	}
}

void presence_uart_drain(size_t bytes_pending, presence_uart_rx_cb_t on_rx, void *ctx,
			 uint32_t *rx_bytes, int64_t *last_rx_us)
{
	uint8_t rx_buf[128];
	size_t remaining = bytes_pending;

	while (remaining > 0U) {
		const size_t chunk = remaining > sizeof(rx_buf) ? sizeof(rx_buf) : remaining;
		int read = uart_read_bytes(UART_NUM_1, rx_buf, chunk, 0);
		if (read <= 0) {
			break;
		}

		if (rx_bytes != NULL) {
			*rx_bytes += (uint32_t)read;
		}
		if (last_rx_us != NULL) {
			*last_rx_us = esp_timer_get_time();
		}
		if (on_rx != NULL) {
			on_rx(rx_buf, (size_t)read, ctx);
		}
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

		if (rx_bytes != NULL) {
			*rx_bytes += (uint32_t)read;
		}
		if (last_rx_us != NULL) {
			*last_rx_us = esp_timer_get_time();
		}
		if (on_rx != NULL) {
			on_rx(rx_buf, (size_t)read, ctx);
		}
	}
}

bool presence_uart_active_recently(int64_t last_rx_us, int64_t now_us)
{
	return (last_rx_us > 0) && ((now_us - last_rx_us) < 1000000);
}
