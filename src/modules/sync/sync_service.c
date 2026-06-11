#include "app_module.h"
#include <core/app_bus.h>
#include <core/module_common.h>
#include <config/settings_model.h>
#include <sync/sync_bus_mapper.h>
#include <sync/sync_config_pull.h>
#include <sync/sync_event_reporter.h>
#include <sync/sync_periodic_timer.h>
#include <sync/sync_request_executor.h>
#include <sync/sync_request_retry.h>
#include <sync/sync_service.h>
#include <sync/sync_todo_cache.h>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>

static const char *TAG = "sync";

#define SYNC_REQUEST_QUEUE_DEPTH 8U

static app_todo_snapshot_t s_todo_snapshot;
static app_device_config_snapshot_t s_device_config_snapshot;
static QueueHandle_t s_request_queue;
static TaskHandle_t s_sync_task;
static sync_periodic_timer_t s_periodic_timer;
static sync_request_retry_state_t s_request_retries;
static SemaphoreHandle_t s_snapshot_mutex;

static void sync_queue_request(const sync_request_t *request);
static void sync_copy_snapshots(app_todo_snapshot_t *todo_snapshot,
				app_device_config_snapshot_t *device_config_snapshot);
static void sync_commit_snapshots(const app_todo_snapshot_t *todo_snapshot,
				  const app_device_config_snapshot_t *device_config_snapshot);
static bool sync_request_updates_snapshots(sync_request_type_t type);

static void sync_publish_error(const char *reason)
{
	app_bus_event_t event = {
		.type = APP_BUS_EVENT_SYNC_FAILED,
	};
	strlcpy(event.data.error.reason, reason != NULL ? reason : "sync failed", sizeof(event.data.error.reason));
	(void)app_bus_publish(&event);
}

static int sync_execute_request(const sync_request_t *request);

static void sync_handle_config_conflict(void *ctx)
{
	(void)ctx;
	ESP_LOGW(TAG, "config version conflict, re-pulling Web config");
	app_device_config_snapshot_t snapshot = { 0 };
	sync_copy_snapshots(NULL, &snapshot);
	if (settings_model_set(&snapshot.settings) == 0) {
		app_bus_event_t updated = {
			.type = APP_BUS_EVENT_WEB_CONFIG_UPDATED,
		};
		updated.data.settings.settings = snapshot.settings;
		(void)app_bus_publish(&updated);
	}
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

static void sync_queue_periodic_request(const sync_request_t *request, void *ctx)
{
	(void)ctx;
	sync_queue_request(request);
}

static void sync_bus_handler(const app_bus_event_t *event, void *ctx)
{
	(void)ctx;
	if (event == NULL) {
		return;
	}

	sync_request_t request = { 0 };
	if (!sync_bus_mapper_event_to_request(event, &request)) {
		return;
	}
	sync_queue_request(&request);
}

static int sync_execute_request(const sync_request_t *request)
{
	app_todo_snapshot_t todo_snapshot = { 0 };
	app_device_config_snapshot_t device_config_snapshot = { 0 };
	sync_copy_snapshots(&todo_snapshot, &device_config_snapshot);

	const sync_request_executor_t executor = {
		.todo_snapshot = &todo_snapshot,
		.device_config_snapshot = &device_config_snapshot,
		.on_config_conflict = sync_handle_config_conflict,
	};
	int ret = sync_request_executor_execute(&executor, request);
	if (ret == 0 && sync_request_updates_snapshots(request->type)) {
		sync_commit_snapshots(&todo_snapshot, &device_config_snapshot);
	}
	return ret;
}

static void sync_task(void *arg)
{
	(void)arg;
	for (;;) {
		sync_request_t request = { 0 };
		if (xQueueReceive(s_request_queue, &request, pdMS_TO_TICKS(1000)) == pdTRUE) {
			if (sync_execute_request(&request) != 0 && sync_request_retryable(request.type)) {
				sync_request_retry_add(&s_request_retries, &request, 0);
			}
		}
		sync_request_retry_process(&s_request_retries, sync_execute_request);
		sync_event_reporter_process_retries();
	}
}

bool sync_service_get_todo_snapshot(app_todo_snapshot_t *out_snapshot)
{
	if (out_snapshot == NULL) {
		return false;
	}
	sync_copy_snapshots(out_snapshot, NULL);
	return true;
}

bool sync_service_get_device_config_snapshot(app_device_config_snapshot_t *out_snapshot)
{
	if (out_snapshot == NULL) {
		return false;
	}
	sync_copy_snapshots(NULL, out_snapshot);
	return true;
}

int sync_service_init(void)
{
	app_settings_t defaults = { 0 };
	settings_model_defaults(&defaults);
	if (s_snapshot_mutex == NULL) {
		s_snapshot_mutex = xSemaphoreCreateMutex();
		if (s_snapshot_mutex == NULL) {
			return -1;
		}
	}
	s_todo_snapshot = (app_todo_snapshot_t){ 0 };
	s_device_config_snapshot = (app_device_config_snapshot_t){
		.config_version = 0,
		.settings = defaults,
	};
	s_request_retries = (sync_request_retry_state_t){ 0 };
	(void)sync_todo_cache_load(&s_todo_snapshot);

	if (s_request_queue == NULL) {
		s_request_queue = xQueueCreate(SYNC_REQUEST_QUEUE_DEPTH, sizeof(sync_request_t));
		if (s_request_queue == NULL) {
			return -1;
		}
	}

	const sync_periodic_timer_config_t periodic_config = {
		.queue_request = sync_queue_periodic_request,
	};
	if (sync_periodic_timer_init(&s_periodic_timer, &periodic_config) != 0) {
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
	sync_periodic_timer_start(&s_periodic_timer);
	sync_queue_request(&(sync_request_t){ .type = SYNC_REQUEST_PULL_CONFIG });
	sync_queue_request(&(sync_request_t){ .type = SYNC_REQUEST_REPORT_STATUS });
	return 0;
}

int sync_service_stop(void)
{
	sync_periodic_timer_stop(&s_periodic_timer);
	if (s_sync_task != NULL) {
		vTaskDelete(s_sync_task);
		s_sync_task = NULL;
	}
	if (s_request_queue != NULL) {
		vQueueDelete(s_request_queue);
		s_request_queue = NULL;
	}
	sync_periodic_timer_deinit(&s_periodic_timer);
	if (s_snapshot_mutex != NULL) {
		vSemaphoreDelete(s_snapshot_mutex);
		s_snapshot_mutex = NULL;
	}
	return 0;
}

static void sync_copy_snapshots(app_todo_snapshot_t *todo_snapshot,
				app_device_config_snapshot_t *device_config_snapshot)
{
	if (s_snapshot_mutex != NULL) {
		if (xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
			if (todo_snapshot != NULL) {
				*todo_snapshot = s_todo_snapshot;
			}
			if (device_config_snapshot != NULL) {
				*device_config_snapshot = s_device_config_snapshot;
			}
			xSemaphoreGive(s_snapshot_mutex);
			return;
		}
	}

	if (todo_snapshot != NULL) {
		*todo_snapshot = s_todo_snapshot;
	}
	if (device_config_snapshot != NULL) {
		*device_config_snapshot = s_device_config_snapshot;
	}
}

static void sync_commit_snapshots(const app_todo_snapshot_t *todo_snapshot,
				  const app_device_config_snapshot_t *device_config_snapshot)
{
	if (todo_snapshot == NULL && device_config_snapshot == NULL) {
		return;
	}

	if (s_snapshot_mutex != NULL) {
		if (xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
			if (todo_snapshot != NULL) {
				s_todo_snapshot = *todo_snapshot;
			}
			if (device_config_snapshot != NULL) {
				s_device_config_snapshot = *device_config_snapshot;
			}
			xSemaphoreGive(s_snapshot_mutex);
			return;
		}
	}

	if (todo_snapshot != NULL) {
		s_todo_snapshot = *todo_snapshot;
	}
	if (device_config_snapshot != NULL) {
		s_device_config_snapshot = *device_config_snapshot;
	}
}

static bool sync_request_updates_snapshots(sync_request_type_t type)
{
	return type == SYNC_REQUEST_PULL_CONFIG || type == SYNC_REQUEST_COMPLETE_TODO ||
	       type == SYNC_REQUEST_DELETE_TODO || type == SYNC_REQUEST_PUSH_ALARMS ||
	       type == SYNC_REQUEST_PUSH_VOICE;
}
