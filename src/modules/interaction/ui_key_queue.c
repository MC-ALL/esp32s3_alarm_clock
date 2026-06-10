#include <interaction/ui_key_queue.h>

#include <core/app_bus.h>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

static const char *TAG = "ui_key_queue";

#define UI_KEY_QUEUE_DEPTH 12U

static QueueHandle_t s_key_queue;

static void ui_key_queue_handle_press(size_t key_index)
{
	if (s_key_queue == NULL) {
		return;
	}

	if (xQueueSend(s_key_queue, &key_index, 0) != pdTRUE) {
		ESP_LOGW(TAG, "key queue full key=%u", (unsigned)(key_index + 1U));
	}
}

static void ui_key_queue_bus_handler(const app_bus_event_t *event, void *ctx)
{
	(void)ctx;
	if (event == NULL || event->type != APP_BUS_EVENT_INPUT_KEY_PRESSED) {
		return;
	}
	ui_key_queue_handle_press((size_t)event->data.input.key_index);
}

int ui_key_queue_init(void)
{
	if (s_key_queue == NULL) {
		s_key_queue = xQueueCreate(UI_KEY_QUEUE_DEPTH, sizeof(size_t));
		if (s_key_queue == NULL) {
			ESP_LOGE(TAG, "failed to create key queue");
			return -1;
		}
	}
	(void)app_bus_subscribe(APP_BUS_EVENT_INPUT_KEY_PRESSED, ui_key_queue_bus_handler, NULL);
	return 0;
}

bool ui_key_queue_receive(size_t *key_index)
{
	if (s_key_queue == NULL || key_index == NULL) {
		return false;
	}
	return xQueueReceive(s_key_queue, key_index, 0) == pdTRUE;
}

void ui_key_queue_deinit(void)
{
	if (s_key_queue != NULL) {
		vQueueDelete(s_key_queue);
		s_key_queue = NULL;
	}
}
