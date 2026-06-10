#ifndef PRESENCE_UART_H_
#define PRESENCE_UART_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

typedef void (*presence_uart_rx_cb_t)(const uint8_t *data, size_t len, void *ctx);

int presence_uart_init(QueueHandle_t *event_queue);
void presence_uart_deinit(void);
void presence_uart_flush_input(QueueHandle_t event_queue);
void presence_uart_drain(size_t bytes_pending, presence_uart_rx_cb_t on_rx, void *ctx,
			 uint32_t *rx_bytes, int64_t *last_rx_us);
bool presence_uart_active_recently(int64_t last_rx_us, int64_t now_us);

#endif
