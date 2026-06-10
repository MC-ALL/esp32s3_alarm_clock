#include "app_module.h"
#include <app_bus.h>
#include <app_config.h>
#include <environment_service.h>
#include <module_common.h>
#include <net_service.h>
#include <presence_service.h>
#include <settings_model.h>
#include <sync_service.h>
#include <sync_protocol.h>

#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <inttypes.h>
#include <nvs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "sync";

#define APP_TODO_CACHE_NAMESPACE "todo_cache"
#define APP_TODO_CACHE_BLOB_KEY "snapshot_v1"
#define APP_TODO_CACHE_MAGIC 0x544F444FU
#define APP_TODO_CACHE_VERSION 1U
#define SYNC_REQUEST_QUEUE_DEPTH 8U
#define SYNC_REQUEST_RETRY_DEPTH 8U
#define SYNC_EVENT_RETRY_DEPTH 8U

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
	char event_type[32];
	char todo_id[24];
	uint8_t attempts;
	int64_t next_due_us;
} sync_event_retry_t;

typedef struct {
	sync_request_t request;
	uint8_t attempts;
	int64_t next_due_us;
} sync_request_retry_t;

typedef struct {
	uint32_t magic;
	uint16_t version;
	uint16_t size;
	app_todo_snapshot_t snapshot;
} sync_todo_cache_blob_t;

static app_todo_snapshot_t s_todo_snapshot;
static app_device_config_snapshot_t s_device_config_snapshot;
static QueueHandle_t s_request_queue;
static TaskHandle_t s_sync_task;
static esp_timer_handle_t s_config_timer;
static esp_timer_handle_t s_status_timer;
static bool s_config_timer_running;
static bool s_status_timer_running;
static bool s_config_in_progress;
static int64_t s_last_config_pull_us;
static int64_t s_last_status_report_us;
static int64_t s_config_backoff_until_us;
static int64_t s_status_backoff_until_us;
static uint8_t s_config_failures;
static uint8_t s_status_failures;
static sync_request_retry_t s_request_retries[SYNC_REQUEST_RETRY_DEPTH];
static uint8_t s_request_retry_count;
static sync_event_retry_t s_event_retries[SYNC_EVENT_RETRY_DEPTH];
static uint8_t s_event_retry_count;

static void sync_queue_request(const sync_request_t *request);

static uint32_t sync_backoff_s(uint8_t failures, uint32_t base_s, uint32_t max_s)
{
	if (failures == 0U) {
		return 0;
	}
	uint32_t value = base_s;
	for (uint8_t i = 1; i < failures && value < max_s; i++) {
		value *= 2U;
	}
	return value > max_s ? max_s : value;
}

static bool sync_net_ready(void)
{
	app_net_status_t status = { 0 };
	return net_service_get_status(&status) && status.wifi_connected && status.ip_ready;
}

static void sync_format_now_iso(char *out, size_t out_size)
{
	if (out == NULL || out_size == 0U) {
		return;
	}

	time_t now = 0;
	struct tm timeinfo = { 0 };
	time(&now);
	localtime_r(&now, &timeinfo);
	(void)strftime(out, out_size, "%Y-%m-%dT%H:%M:%S%z", &timeinfo);
}

static void sync_todo_cache_sanitize(app_todo_snapshot_t *snapshot)
{
	if (snapshot == NULL) {
		return;
	}
	if (snapshot->count > APP_TODO_MAX_ITEMS) {
		snapshot->count = APP_TODO_MAX_ITEMS;
	}
	snapshot->sync_in_progress = false;
	if (!snapshot->sync_ok) {
		snapshot->last_error[0] = '\0';
	}
}

static void sync_todo_cache_save(void)
{
	sync_todo_cache_blob_t blob = {
		.magic = APP_TODO_CACHE_MAGIC,
		.version = APP_TODO_CACHE_VERSION,
		.size = sizeof(blob.snapshot),
		.snapshot = s_todo_snapshot,
	};
	sync_todo_cache_sanitize(&blob.snapshot);

	nvs_handle_t handle = 0;
	esp_err_t err = nvs_open(APP_TODO_CACHE_NAMESPACE, NVS_READWRITE, &handle);
	if (err != ESP_OK) {
		ESP_LOGW(TAG, "todo cache open write failed: %s", esp_err_to_name(err));
		return;
	}
	err = nvs_set_blob(handle, APP_TODO_CACHE_BLOB_KEY, &blob, sizeof(blob));
	if (err == ESP_OK) {
		err = nvs_commit(handle);
	}
	nvs_close(handle);
	if (err != ESP_OK) {
		ESP_LOGW(TAG, "todo cache save failed: %s", esp_err_to_name(err));
	}
}

static bool sync_todo_cache_load(void)
{
	nvs_handle_t handle = 0;
	esp_err_t err = nvs_open(APP_TODO_CACHE_NAMESPACE, NVS_READONLY, &handle);
	if (err != ESP_OK) {
		return false;
	}

	sync_todo_cache_blob_t blob = { 0 };
	size_t size = sizeof(blob);
	err = nvs_get_blob(handle, APP_TODO_CACHE_BLOB_KEY, &blob, &size);
	nvs_close(handle);
	if (err != ESP_OK || size != sizeof(blob) || blob.magic != APP_TODO_CACHE_MAGIC ||
	    blob.version != APP_TODO_CACHE_VERSION || blob.size != sizeof(blob.snapshot)) {
		return false;
	}

	sync_todo_cache_sanitize(&blob.snapshot);
	s_todo_snapshot = blob.snapshot;
	return true;
}

static int sync_find_todo_index_by_id(const char *todo_id)
{
	if (todo_id == NULL || todo_id[0] == '\0') {
		return -1;
	}
	for (uint8_t i = 0; i < s_todo_snapshot.count && i < APP_TODO_MAX_ITEMS; i++) {
		if (strcmp(s_todo_snapshot.items[i].id, todo_id) == 0) {
			return (int)i;
		}
	}
	return -1;
}

static void sync_remove_todo_from_cache(const char *todo_id)
{
	const int index = sync_find_todo_index_by_id(todo_id);
	if (index < 0) {
		return;
	}
	for (uint8_t i = (uint8_t)index; i + 1U < s_todo_snapshot.count; i++) {
		s_todo_snapshot.items[i] = s_todo_snapshot.items[i + 1U];
	}
	if (s_todo_snapshot.count > 0U) {
		s_todo_snapshot.count--;
		s_todo_snapshot.items[s_todo_snapshot.count] = (app_todo_item_t){ 0 };
	}
}

static void sync_touch_last_sync(void)
{
	time_t now = 0;
	struct tm timeinfo = { 0 };
	time(&now);
	localtime_r(&now, &timeinfo);
	(void)snprintf(s_todo_snapshot.last_sync_at, sizeof(s_todo_snapshot.last_sync_at), "%02d:%02d:%02d",
		       timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

static void sync_publish_error(const char *reason)
{
	app_bus_event_t event = {
		.type = APP_BUS_EVENT_SYNC_FAILED,
	};
	strlcpy(event.data.error.reason, reason != NULL ? reason : "sync failed", sizeof(event.data.error.reason));
	(void)app_bus_publish(&event);
}

static void sync_mark_config_failure(const char *reason)
{
	s_config_failures++;
	uint32_t backoff_s = sync_backoff_s(s_config_failures, 5U, 120U);
	s_config_backoff_until_us = esp_timer_get_time() + (int64_t)backoff_s * 1000000LL;
	s_todo_snapshot.sync_ok = false;
	s_todo_snapshot.sync_in_progress = false;
	strlcpy(s_todo_snapshot.last_error, reason != NULL ? reason : "config sync failed",
		sizeof(s_todo_snapshot.last_error));
	sync_publish_error(s_todo_snapshot.last_error);
}

static int sync_http_request(const char *method, const char *path, const char *request_body,
			     char *response_body, size_t response_body_size, int *out_status)
{
	char url[192];
	(void)snprintf(url, sizeof(url), "http://%s:%d%s", APP_TODO_WEB_HOST, APP_TODO_WEB_PORT, path);

	app_net_http_response_t response = {
		.body = response_body,
		.body_cap = response_body_size,
	};
	if (response_body != NULL && response_body_size > 0U) {
		response_body[0] = '\0';
	}

	int ret = net_service_http_request(method, url, request_body, &response);
	if (out_status != NULL) {
		*out_status = response.status_code;
	}
	return ret;
}

static void sync_pull_config_once(void)
{
	const int64_t now_us = esp_timer_get_time();
	if (s_config_in_progress || now_us < s_config_backoff_until_us) {
		return;
	}
	if (!sync_net_ready()) {
		sync_mark_config_failure("wifi offline");
		return;
	}

	static const size_t BODY_CAP = 2048U;
	char *body = (char *)calloc(1, BODY_CAP);
	app_todo_snapshot_t *next_snapshot = (app_todo_snapshot_t *)malloc(sizeof(*next_snapshot));
	if (body == NULL || next_snapshot == NULL) {
		free(body);
		free(next_snapshot);
		sync_mark_config_failure("sync oom");
		return;
	}

	*next_snapshot = s_todo_snapshot;
	app_device_config_snapshot_t next_config = s_device_config_snapshot;
	s_config_in_progress = true;
	s_todo_snapshot.sync_in_progress = true;

	int status = 0;
	int ret = sync_http_request("GET", APP_DEVICE_CONFIG_WEB_PATH, NULL, body, BODY_CAP, &status);
	app_settings_t current_settings = { 0 };
	settings_model_get(&current_settings);
	if (ret == 0 && status == 200 &&
	    sync_protocol_parse_device_config(body, &current_settings, &next_config, next_snapshot)) {
		next_snapshot->sync_ok = true;
		next_snapshot->sync_in_progress = false;
		next_snapshot->last_error[0] = '\0';
		s_todo_snapshot = *next_snapshot;

		s_device_config_snapshot = next_config;
		if (memcmp(&current_settings, &s_device_config_snapshot.settings, sizeof(current_settings)) != 0) {
			(void)settings_model_set(&s_device_config_snapshot.settings);
			app_bus_event_t updated = {
				.type = APP_BUS_EVENT_WEB_CONFIG_UPDATED,
			};
			updated.data.settings.settings = s_device_config_snapshot.settings;
			(void)app_bus_publish(&updated);
		}
		sync_touch_last_sync();
		sync_todo_cache_save();
		s_config_failures = 0;
		s_config_backoff_until_us = 0;
		ESP_LOGI(TAG, "config pull ok count=%u cfg=%u bytes=%u",
			 (unsigned)s_todo_snapshot.count, (unsigned)s_device_config_snapshot.config_version,
			 (unsigned)strlen(body));
	} else {
		char reason[64];
		(void)snprintf(reason, sizeof(reason), "config pull err=%d status=%d", ret, status);
		sync_mark_config_failure(reason);
		ESP_LOGW(TAG, "%s heap=%u", reason, (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
	}

	free(next_snapshot);
	free(body);
	s_todo_snapshot.sync_in_progress = false;
	s_config_in_progress = false;
}

static void sync_report_status_once(void)
{
	const int64_t now_us = esp_timer_get_time();
	if (now_us < s_status_backoff_until_us || !sync_net_ready()) {
		return;
	}

	app_environment_snapshot_t env = { 0 };
	app_presence_status_t presence = { 0 };
	char sent_at[40] = { 0 };
	(void)environment_service_get_snapshot(&env);
	(void)presence_service_get_status(&presence);
	sync_format_now_iso(sent_at, sizeof(sent_at));

	char *body = sync_protocol_build_status_report(sent_at, &env, &presence);
	if (body == NULL) {
		sync_publish_error("status report oom");
		return;
	}

	char response[64] = { 0 };
	int status = 0;
	int ret = sync_http_request("POST", APP_DEVICE_STATUS_WEB_PATH, body, response, sizeof(response), &status);
	free(body);
	if (ret == 0 && status == 200) {
		s_status_failures = 0;
		s_status_backoff_until_us = 0;
		return;
	}

	s_status_failures++;
	uint32_t backoff_s = sync_backoff_s(s_status_failures, 5U, 60U);
	s_status_backoff_until_us = now_us + (int64_t)backoff_s * 1000000LL;
	ESP_LOGW(TAG, "status report failed ret=%d status=%d backoff=%us", ret, status, (unsigned)backoff_s);
}

static int sync_report_event_http(const char *event_type, const char *todo_id)
{
	if (event_type == NULL || event_type[0] == '\0' || !sync_net_ready()) {
		return -1;
	}

	app_environment_snapshot_t env = { 0 };
	app_presence_status_t presence = { 0 };
	char event_at[40] = { 0 };
	(void)environment_service_get_snapshot(&env);
	(void)presence_service_get_status(&presence);
	sync_format_now_iso(event_at, sizeof(event_at));

	char *body = sync_protocol_build_event_report(event_at, event_type, todo_id, &env, &presence);
	if (body == NULL) {
		return -1;
	}

	char response[64] = { 0 };
	int status = 0;
	int ret = sync_http_request("POST", APP_DEVICE_EVENTS_WEB_PATH, body, response, sizeof(response), &status);
	free(body);
	return ret == 0 && status == 200 ? 0 : -1;
}

static void sync_retry_event_add(const char *event_type, const char *todo_id, uint8_t attempts)
{
	if (event_type == NULL || event_type[0] == '\0') {
		return;
	}
	if (s_event_retry_count >= SYNC_EVENT_RETRY_DEPTH) {
		memmove(&s_event_retries[0], &s_event_retries[1], sizeof(s_event_retries[0]) * (SYNC_EVENT_RETRY_DEPTH - 1U));
		s_event_retry_count = SYNC_EVENT_RETRY_DEPTH - 1U;
	}

	sync_event_retry_t *slot = &s_event_retries[s_event_retry_count++];
	memset(slot, 0, sizeof(*slot));
	strlcpy(slot->event_type, event_type, sizeof(slot->event_type));
	if (todo_id != NULL) {
		strlcpy(slot->todo_id, todo_id, sizeof(slot->todo_id));
	}
	slot->attempts = attempts;
	slot->next_due_us = esp_timer_get_time() + (int64_t)sync_backoff_s(attempts + 1U, 5U, 300U) * 1000000LL;
}

static void sync_report_event_once(const char *event_type, const char *todo_id)
{
	if (sync_report_event_http(event_type, todo_id) != 0) {
		sync_retry_event_add(event_type, todo_id, 0);
	}
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

static void sync_process_event_retries(void)
{
	const int64_t now_us = esp_timer_get_time();
	for (uint8_t i = 0; i < s_event_retry_count;) {
		sync_event_retry_t *retry = &s_event_retries[i];
		if (now_us < retry->next_due_us) {
			i++;
			continue;
		}
		if (sync_report_event_http(retry->event_type, retry->todo_id) == 0) {
			memmove(retry, retry + 1, sizeof(*retry) * (s_event_retry_count - i - 1U));
			s_event_retry_count--;
			continue;
		}
		retry->attempts++;
		retry->next_due_us = now_us + (int64_t)sync_backoff_s(retry->attempts, 5U, 300U) * 1000000LL;
		i++;
	}
}

static int sync_complete_todo_once(const char *todo_id)
{
	if (todo_id == NULL || todo_id[0] == '\0' || !sync_net_ready()) {
		return -1;
	}

	char path[128];
	(void)snprintf(path, sizeof(path), "%s/%s/complete", APP_TODO_WEB_PATH, todo_id);
	char response[256] = { 0 };
	int status = 0;
	int ret = sync_http_request("POST", path, NULL, response, sizeof(response), &status);
	if (ret != 0 || (status != 200 && status != 404)) {
		(void)snprintf(s_todo_snapshot.last_error, sizeof(s_todo_snapshot.last_error),
			       "todo complete err=%d status=%d", ret, status);
		sync_publish_error(s_todo_snapshot.last_error);
		return -1;
	}

	sync_remove_todo_from_cache(todo_id);
	s_todo_snapshot.sync_ok = true;
	s_todo_snapshot.last_error[0] = '\0';
	sync_touch_last_sync();
	sync_todo_cache_save();
	sync_report_event_once("todo_completed", todo_id);
	return 0;
}

static int sync_delete_todo_once(const char *todo_id)
{
	if (todo_id == NULL || todo_id[0] == '\0' || !sync_net_ready()) {
		return -1;
	}

	char path[128];
	(void)snprintf(path, sizeof(path), "%s/%s", APP_TODO_WEB_PATH, todo_id);
	char response[64] = { 0 };
	int status = 0;
	int ret = sync_http_request("DELETE", path, NULL, response, sizeof(response), &status);
	if (ret != 0 || (status != 204 && status != 404)) {
		(void)snprintf(s_todo_snapshot.last_error, sizeof(s_todo_snapshot.last_error),
			       "todo delete err=%d status=%d", ret, status);
		sync_publish_error(s_todo_snapshot.last_error);
		return -1;
	}

	sync_remove_todo_from_cache(todo_id);
	s_todo_snapshot.sync_ok = true;
	s_todo_snapshot.last_error[0] = '\0';
	sync_touch_last_sync();
	sync_todo_cache_save();
	sync_report_event_once("todo_deleted", todo_id);
	return 0;
}

static void sync_handle_config_conflict(void)
{
	ESP_LOGW(TAG, "config version conflict, re-pulling Web config");
	s_config_backoff_until_us = 0;
	sync_queue_request(&(sync_request_t){ .type = SYNC_REQUEST_PULL_CONFIG });
}

static int sync_push_alarm_settings_once(const app_settings_t *settings)
{
	if (settings == NULL || !sync_net_ready()) {
		return -1;
	}

	char *body = sync_protocol_build_alarm_settings(s_device_config_snapshot.config_version, settings);
	if (body == NULL) {
		sync_publish_error("alarm push oom");
		return -1;
	}

	char response[256] = { 0 };
	int status = 0;
	int ret = sync_http_request("PUT", "/api/device/alarms", body, response, sizeof(response), &status);
	free(body);
	if (ret == 0 && status == 200) {
		s_device_config_snapshot.settings = *settings;
		sync_protocol_parse_config_version_response(response, &s_device_config_snapshot.config_version,
							    s_device_config_snapshot.updated_at,
							    sizeof(s_device_config_snapshot.updated_at));
		return 0;
	}
	if (status == 409) {
		sync_handle_config_conflict();
		return 0;
	}
	sync_publish_error("alarm push failed");
	return -1;
}

static int sync_push_voice_settings_once(const app_settings_t *settings)
{
	if (settings == NULL || !sync_net_ready()) {
		return -1;
	}

	char *body = sync_protocol_build_voice_settings(s_device_config_snapshot.config_version, settings);
	if (body == NULL) {
		sync_publish_error("voice push oom");
		return -1;
	}

	char response[256] = { 0 };
	int status = 0;
	int ret = sync_http_request("PUT", "/api/device/voice-settings", body, response, sizeof(response), &status);
	free(body);
	if (ret == 0 && status == 200) {
		s_device_config_snapshot.settings = *settings;
		sync_protocol_parse_config_version_response(response, &s_device_config_snapshot.config_version,
							    s_device_config_snapshot.updated_at,
							    sizeof(s_device_config_snapshot.updated_at));
		return 0;
	}
	if (status == 409) {
		sync_handle_config_conflict();
		return 0;
	}
	sync_publish_error("voice push failed");
	return -1;
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
		sync_pull_config_once();
		return 0;
	}
	if (request->type == SYNC_REQUEST_REPORT_STATUS) {
		sync_report_status_once();
		return 0;
	}
	if (request->type == SYNC_REQUEST_COMPLETE_TODO) {
		return sync_complete_todo_once(request->todo_id);
	}
	if (request->type == SYNC_REQUEST_DELETE_TODO) {
		return sync_delete_todo_once(request->todo_id);
	}
	if (request->type == SYNC_REQUEST_PUSH_ALARMS) {
		return sync_push_alarm_settings_once(&request->settings);
	}
	if (request->type == SYNC_REQUEST_PUSH_VOICE) {
		return sync_push_voice_settings_once(&request->settings);
	}
	if (request->type == SYNC_REQUEST_REPORT_EVENT) {
		sync_report_event_once(request->event_type, request->todo_id);
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
		sync_process_event_retries();
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
	(void)sync_todo_cache_load();

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
