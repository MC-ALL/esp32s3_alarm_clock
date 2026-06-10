#include "app_module.h"
#include <app_bus.h>
#include <app_config.h>
#include <module_common.h>
#include <settings_model.h>
#include <sync_config_pull.h>
#include <sync_event_reporter.h>
#include <sync_settings_push.h>
#include <sync_service.h>
#include <sync_status_reporter.h>
#include <sync_todo_cache.h>
#include <sync_todo_ops.h>
#include <sync_transport.h>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string.h>

static const char *TAG = "sync";

#define SYNC_REQUEST_QUEUE_DEPTH 8U
#define SYNC_REQUEST_RETRY_DEPTH 8U

typedef enum {
	SYNC_REQUEST_PULL_CONFIG = 1,
	SYNC_REQUEST_REPORT_STATUS,
	SYNC_REQUEST_COMPLETE_TODO,
	SYNC_REQUEST_DELETE_TODO,
	SYNC_REQUEST_PUSH_ALARMS,
	SYNC_REQUEST_PUSH_VOICE,
	SYNC_REQUEST_REPORT_EVENT,
} sync_request_type_t;

typedef struct {
	sync_request_type_t type;
	char todo_id[24];
	char event_type[32];
	app_settings_t settings;
} sync_request_t;

typedef struct {
	sync_request_t request;
	uint8_t attempts;
	int64_t next_due_us;
} sync_request_retry_t;

static app_todo_snapshot_t s_todo_snapshot;
static app_device_config_snapshot_t s_device_config_snapshot;
static QueueHandle_t s_request_queue;
static TaskHandle_t s_sync_task;
static esp_timer_handle_t s_config_timer;
static esp_timer_handle_t s_status_timer;
static bool s_config_timer_running;
static bool s_status_timer_running;
static int64_t s_last_config_pull_us;
static int64_t s_last_status_report_us;
static sync_request_retry_t s_request_retries[SYNC_REQUEST_RETRY_DEPTH];
static uint8_t s_request_retry_count;

static void sync_queue_request(const sync_request_t *request);

static void sync_publish_error(const char *reason)
{
	app_bus_event_t event = {
		.type = APP_BUS_EVENT_SYNC_FAILED,
	};
	strlcpy(event.data.error.reason, reason != NULL ? reason : "sync failed", sizeof(event.data.error.reason));
	(void)app_bus_publish(&event);
}

static void sync_retry_request_add(const sync_request_t *request, uint8_t attempts)
{
	if (request == NULL) {
		return;
	}
	if (request->type == SYNC_REQUEST_PUSH_ALARMS || request->type == SYNC_REQUEST_PUSH_VOICE) {
		for (uint8_t i = 0; i < s_request_retry_count; i++) {
			if (s_request_retries[i].request.type == request->type) {
				s_request_retries[i].request = *request;
				s_request_retries[i].attempts = attempts;
				s_request_retries[i].next_due_us =
					esp_timer_get_time() +
					(int64_t)sync_backoff_s(attempts + 1U, 5U, 300U) * 1000000LL;
				return;
			}
		}
	}
	if (s_request_retry_count >= SYNC_REQUEST_RETRY_DEPTH) {
		memmove(&s_request_retries[0], &s_request_retries[1],
			sizeof(s_request_retries[0]) * (SYNC_REQUEST_RETRY_DEPTH - 1U));
		s_request_retry_count = SYNC_REQUEST_RETRY_DEPTH - 1U;
	}

	sync_request_retry_t *slot = &s_request_retries[s_request_retry_count++];
	memset(slot, 0, sizeof(*slot));
	slot->request = *request;
	slot->attempts = attempts;
	slot->next_due_us =
		esp_timer_get_time() + (int64_t)sync_backoff_s(attempts + 1U, 5U, 300U) * 1000000LL;
}

static bool sync_request_retryable(sync_request_type_t type)
{
	return type == SYNC_REQUEST_COMPLETE_TODO || type == SYNC_REQUEST_DELETE_TODO ||
	       type == SYNC_REQUEST_PUSH_ALARMS || type == SYNC_REQUEST_PUSH_VOICE;
}

static int sync_execute_request(const sync_request_t *request);

static void sync_process_request_retries(void)
{
	const int64_t now_us = esp_timer_get_time();
	for (uint8_t i = 0; i < s_request_retry_count;) {
		sync_request_retry_t *retry = &s_request_retries[i];
		if (now_us < retry->next_due_us) {
			i++;
			continue;
		}
		if (sync_execute_request(&retry->request) == 0) {
			memmove(retry, retry + 1, sizeof(*retry) * (s_request_retry_count - i - 1U));
			s_request_retry_count--;
			continue;
		}
		retry->attempts++;
		retry->next_due_us = now_us + (int64_t)sync_backoff_s(retry->attempts, 5U, 300U) * 1000000LL;
		i++;
	}
}

static void sync_handle_config_conflict(void *ctx)
{
	(void)ctx;
	ESP_LOGW(TAG, "config version conflict, re-pulling Web config");
	sync_config_pull_reset_backoff();
	sync_queue_request(&(sync_request_t){ .type = SYNC_REQUEST_PULL_CONFIG });
}

static void sync_queue_request(const sync_request_t *request)
{
	if (request == NULL || s_request_queue == NULL) {
		return;
	}
	if (xQueueSend(s_request_queue, request, 0) != pdTRUE) {
		sync_publish_error("sync request queue full");
	}
}

static void sync_bus_handler(const app_bus_event_t *event, void *ctx)
{
	(void)ctx;
	if (event == NULL) {
		return;
	}

	sync_request_t request = { 0 };
	if (event->type == APP_BUS_EVENT_TODO_SYNC_REQUEST) {
		request.type = SYNC_REQUEST_PULL_CONFIG;
	} else if (event->type == APP_BUS_EVENT_TODO_COMPLETE_REQUEST) {
		request.type = SYNC_REQUEST_COMPLETE_TODO;
		strlcpy(request.todo_id, event->data.todo.todo_id, sizeof(request.todo_id));
	} else if (event->type == APP_BUS_EVENT_TODO_DELETE_REQUEST) {
		request.type = SYNC_REQUEST_DELETE_TODO;
		strlcpy(request.todo_id, event->data.todo.todo_id, sizeof(request.todo_id));
	} else if (event->type == APP_BUS_EVENT_ALARM_SETTINGS_CHANGED) {
		request.type = SYNC_REQUEST_PUSH_ALARMS;
		request.settings = event->data.settings.settings;
	} else if (event->type == APP_BUS_EVENT_VOICE_SETTINGS_CHANGED) {
		request.type = SYNC_REQUEST_PUSH_VOICE;
		request.settings = event->data.settings.settings;
	} else if (event->type == APP_BUS_EVENT_DEVICE_EVENT) {
		request.type = SYNC_REQUEST_REPORT_EVENT;
		strlcpy(request.event_type, event->data.device_event.event_type, sizeof(request.event_type));
		strlcpy(request.todo_id, event->data.device_event.todo_id, sizeof(request.todo_id));
	} else {
		return;
	}
	sync_queue_request(&request);
}

static void sync_config_timer_cb(void *arg)
{
	(void)arg;
	const int64_t now_us = esp_timer_get_time();
	if (s_last_config_pull_us > 0 &&
	    (now_us - s_last_config_pull_us) < (int64_t)APP_CONFIG_SYNC_INTERVAL_S * 1000000LL) {
		return;
	}
	s_last_config_pull_us = now_us;
	sync_queue_request(&(sync_request_t){ .type = SYNC_REQUEST_PULL_CONFIG });
}

static void sync_status_timer_cb(void *arg)
{
	(void)arg;
	const int64_t now_us = esp_timer_get_time();
	if (s_last_status_report_us > 0 &&
	    (now_us - s_last_status_report_us) < (int64_t)APP_STATUS_REPORT_INTERVAL_S * 1000000LL) {
		return;
	}
	s_last_status_report_us = now_us;
	sync_queue_request(&(sync_request_t){ .type = SYNC_REQUEST_REPORT_STATUS });
}

static int sync_execute_request(const sync_request_t *request)
{
	if (request == NULL) {
		return -1;
	}
	if (request->type == SYNC_REQUEST_PULL_CONFIG) {
		sync_config_pull_once(&s_todo_snapshot, &s_device_config_snapshot);
		return 0;
	}
	if (request->type == SYNC_REQUEST_REPORT_STATUS) {
		sync_status_reporter_report_once();
		return 0;
	}
	if (request->type == SYNC_REQUEST_COMPLETE_TODO) {
		return sync_todo_ops_complete_once(&s_todo_snapshot, request->todo_id);
	}
	if (request->type == SYNC_REQUEST_DELETE_TODO) {
		return sync_todo_ops_delete_once(&s_todo_snapshot, request->todo_id);
	}
	if (request->type == SYNC_REQUEST_PUSH_ALARMS) {
		return sync_settings_push_alarms_once(&s_device_config_snapshot, &request->settings,
						      sync_handle_config_conflict, NULL);
	}
	if (request->type == SYNC_REQUEST_PUSH_VOICE) {
		return sync_settings_push_voice_once(&s_device_config_snapshot, &request->settings,
						     sync_handle_config_conflict, NULL);
	}
	if (request->type == SYNC_REQUEST_REPORT_EVENT) {
		sync_event_reporter_report_once(request->event_type, request->todo_id);
		return 0;
	}
	return -1;
}

static void sync_task(void *arg)
{
	(void)arg;
	for (;;) {
		sync_request_t request = { 0 };
		if (xQueueReceive(s_request_queue, &request, pdMS_TO_TICKS(1000)) == pdTRUE) {
			if (sync_execute_request(&request) != 0 && sync_request_retryable(request.type)) {
				sync_retry_request_add(&request, 0);
			}
		}
		sync_process_request_retries();
		sync_event_reporter_process_retries();
	}
}

bool sync_service_get_todo_snapshot(app_todo_snapshot_t *out_snapshot)
{
	if (out_snapshot == NULL) {
		return false;
	}
	*out_snapshot = s_todo_snapshot;
	return true;
}

bool sync_service_get_device_config_snapshot(app_device_config_snapshot_t *out_snapshot)
{
	if (out_snapshot == NULL) {
		return false;
	}
	*out_snapshot = s_device_config_snapshot;
	return true;
}

int sync_service_init(void)
{
	app_settings_t defaults = { 0 };
	settings_model_defaults(&defaults);
	s_todo_snapshot = (app_todo_snapshot_t){ 0 };
	s_device_config_snapshot = (app_device_config_snapshot_t){
		.config_version = 0,
		.settings = defaults,
	};
	(void)sync_todo_cache_load(&s_todo_snapshot);

	if (s_request_queue == NULL) {
		s_request_queue = xQueueCreate(SYNC_REQUEST_QUEUE_DEPTH, sizeof(sync_request_t));
		if (s_request_queue == NULL) {
			return -1;
		}
	}

	const esp_timer_create_args_t config_timer_args = {
		.callback = sync_config_timer_cb,
		.name = "config_sync",
	};
	if (s_config_timer == NULL && esp_timer_create(&config_timer_args, &s_config_timer) != ESP_OK) {
		return -1;
	}
	const esp_timer_create_args_t status_timer_args = {
		.callback = sync_status_timer_cb,
		.name = "status_report",
	};
	if (s_status_timer == NULL && esp_timer_create(&status_timer_args, &s_status_timer) != ESP_OK) {
		return -1;
	}

	(void)app_bus_subscribe(APP_BUS_EVENT_TODO_SYNC_REQUEST, sync_bus_handler, NULL);
	(void)app_bus_subscribe(APP_BUS_EVENT_TODO_COMPLETE_REQUEST, sync_bus_handler, NULL);
	(void)app_bus_subscribe(APP_BUS_EVENT_TODO_DELETE_REQUEST, sync_bus_handler, NULL);
	(void)app_bus_subscribe(APP_BUS_EVENT_ALARM_SETTINGS_CHANGED, sync_bus_handler, NULL);
	(void)app_bus_subscribe(APP_BUS_EVENT_VOICE_SETTINGS_CHANGED, sync_bus_handler, NULL);
	(void)app_bus_subscribe(APP_BUS_EVENT_DEVICE_EVENT, sync_bus_handler, NULL);
	return 0;
}

int sync_service_start(void)
{
	if (s_sync_task == NULL) {
		BaseType_t ok = xTaskCreate(sync_task, "sync_service", 6144, NULL, 6, &s_sync_task);
		if (ok != pdPASS) {
			return -1;
		}
	}
	if (s_config_timer != NULL && !s_config_timer_running &&
	    esp_timer_start_periodic(s_config_timer, (uint64_t)APP_CONFIG_SYNC_INTERVAL_S * 1000000ULL) == ESP_OK) {
		s_config_timer_running = true;
	}
	if (s_status_timer != NULL && !s_status_timer_running &&
	    esp_timer_start_periodic(s_status_timer, (uint64_t)APP_STATUS_REPORT_INTERVAL_S * 1000000ULL) == ESP_OK) {
		s_status_timer_running = true;
	}
	sync_queue_request(&(sync_request_t){ .type = SYNC_REQUEST_PULL_CONFIG });
	sync_queue_request(&(sync_request_t){ .type = SYNC_REQUEST_REPORT_STATUS });
	return 0;
}

int sync_service_stop(void)
{
	if (s_config_timer != NULL && s_config_timer_running) {
		(void)esp_timer_stop(s_config_timer);
		s_config_timer_running = false;
	}
	if (s_status_timer != NULL && s_status_timer_running) {
		(void)esp_timer_stop(s_status_timer);
		s_status_timer_running = false;
	}
	if (s_sync_task != NULL) {
		vTaskDelete(s_sync_task);
		s_sync_task = NULL;
	}
	if (s_request_queue != NULL) {
		vQueueDelete(s_request_queue);
		s_request_queue = NULL;
	}
	if (s_config_timer != NULL) {
		(void)esp_timer_delete(s_config_timer);
		s_config_timer = NULL;
	}
	if (s_status_timer != NULL) {
		(void)esp_timer_delete(s_status_timer);
		s_status_timer = NULL;
	}
	return 0;
}
