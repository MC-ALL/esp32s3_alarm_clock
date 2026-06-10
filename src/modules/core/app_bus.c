#include "app_module.h"
#include <core/app_bus.h>
#include <core/module_common.h>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string.h>

static const char *TAG = "app_bus";

#define APP_BUS_MAX_SUBSCRIBERS 16U
#define APP_BUS_QUEUE_DEPTH 16U

typedef struct {
	app_bus_event_type_t type;
	app_bus_handler_t handler;
	void *ctx;
} app_bus_subscription_t;

static QueueHandle_t s_event_queue;
static TaskHandle_t s_bus_task;
static app_bus_subscription_t s_subscriptions[APP_BUS_MAX_SUBSCRIBERS];
static size_t s_subscription_count;

int app_bus_subscribe(app_bus_event_type_t type, app_bus_handler_t handler, void *ctx)
{
	if (type == 0 || handler == NULL || s_subscription_count >= APP_BUS_MAX_SUBSCRIBERS) {
		return -1;
	}

	s_subscriptions[s_subscription_count++] = (app_bus_subscription_t){
		.type = type,
		.handler = handler,
		.ctx = ctx,
	};
	return 0;
}

int app_bus_publish(const app_bus_event_t *event)
{
	if (event == NULL || event->type == 0 || s_event_queue == NULL) {
		return -1;
	}

	app_bus_event_t copy = *event;
	if (copy.timestamp_us == 0) {
		copy.timestamp_us = esp_timer_get_time();
	}

	if (xQueueSend(s_event_queue, &copy, 0) != pdTRUE) {
		ESP_LOGW(TAG, "drop event type=%d queue full", (int)copy.type);
		return -1;
	}
	return 0;
}

int app_bus_init(void)
{
	if (s_event_queue != NULL) {
		return 0;
	}

	s_event_queue = xQueueCreate(APP_BUS_QUEUE_DEPTH, sizeof(app_bus_event_t));
	if (s_event_queue == NULL) {
		ESP_LOGE(TAG, "failed to create event queue");
		return -1;
	}
	s_subscription_count = 0;
	memset(s_subscriptions, 0, sizeof(s_subscriptions));
	return 0;
}

static bool app_bus_dispatch_once(TickType_t timeout_ticks)
{
	if (s_event_queue == NULL) {
		return false;
	}

	app_bus_event_t event = { 0 };
	if (xQueueReceive(s_event_queue, &event, timeout_ticks) != pdTRUE) {
		return false;
	}

	for (size_t i = 0; i < s_subscription_count; i++) {
		const app_bus_subscription_t *sub = &s_subscriptions[i];
		if (sub->type == event.type && sub->handler != NULL) {
			sub->handler(&event, sub->ctx);
		}
	}
	return true;
}

static void app_bus_task(void *arg)
{
	(void)arg;

	for (;;) {
		(void)app_bus_dispatch_once(portMAX_DELAY);
	}
}

int app_bus_start(void)
{
	if (s_event_queue == NULL || s_bus_task != NULL) {
		return 0;
	}

	BaseType_t ok = xTaskCreate(app_bus_task, "app_bus", 4096, NULL, 7, &s_bus_task);
	if (ok != pdPASS) {
		ESP_LOGE(TAG, "failed to create dispatch task");
		return -1;
	}
	return 0;
}

int app_bus_stop(void)
{
	if (s_bus_task != NULL) {
		vTaskDelete(s_bus_task);
		s_bus_task = NULL;
	}
	if (s_event_queue != NULL) {
		vQueueDelete(s_event_queue);
		s_event_queue = NULL;
	}
	s_subscription_count = 0;
	memset(s_subscriptions, 0, sizeof(s_subscriptions));
	return 0;
}
