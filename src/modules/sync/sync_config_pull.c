#include <sync/sync_config_pull.h>

#include <core/app_bus.h>
#include <core/app_config.h>
#include <config/settings_model.h>
#include <sync/sync_protocol.h>
#include <sync/sync_todo_cache.h>
#include <sync/sync_transport.h>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "sync_config";

static bool s_config_in_progress;
static int64_t s_config_backoff_until_us;
static uint8_t s_config_failures;

static void sync_config_pull_touch_last_sync(app_todo_snapshot_t *todo_snapshot)
{
	time_t now = 0;
	struct tm timeinfo = { 0 };
	time(&now);
	localtime_r(&now, &timeinfo);
	(void)snprintf(todo_snapshot->last_sync_at, sizeof(todo_snapshot->last_sync_at), "%02d:%02d:%02d",
		       timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

static void sync_config_pull_publish_error(const char *reason)
{
	app_bus_event_t event = {
		.type = APP_BUS_EVENT_SYNC_FAILED,
	};
	strlcpy(event.data.error.reason, reason != NULL ? reason : "sync failed", sizeof(event.data.error.reason));
	(void)app_bus_publish(&event);
}

static void sync_config_pull_mark_failure(app_todo_snapshot_t *todo_snapshot, const char *reason)
{
	s_config_failures++;
	uint32_t backoff_s = sync_backoff_s(s_config_failures, 5U, 120U);
	s_config_backoff_until_us = esp_timer_get_time() + (int64_t)backoff_s * 1000000LL;
	todo_snapshot->sync_ok = false;
	todo_snapshot->sync_in_progress = false;
	strlcpy(todo_snapshot->last_error, reason != NULL ? reason : "config sync failed", sizeof(todo_snapshot->last_error));
	sync_config_pull_publish_error(todo_snapshot->last_error);
}

void sync_config_pull_reset_backoff(void)
{
	s_config_backoff_until_us = 0;
}

void sync_config_pull_once(app_todo_snapshot_t *todo_snapshot, app_device_config_snapshot_t *device_config_snapshot)
{
	if (todo_snapshot == NULL || device_config_snapshot == NULL) {
		return;
	}

	const int64_t now_us = esp_timer_get_time();
	if (s_config_in_progress || now_us < s_config_backoff_until_us) {
		return;
	}
	if (!sync_transport_net_ready()) {
		sync_config_pull_mark_failure(todo_snapshot, "wifi offline");
		return;
	}

	static const size_t BODY_CAP = 2048U;
	char *body = (char *)calloc(1, BODY_CAP);
	app_todo_snapshot_t *next_snapshot = (app_todo_snapshot_t *)malloc(sizeof(*next_snapshot));
	if (body == NULL || next_snapshot == NULL) {
		free(body);
		free(next_snapshot);
		sync_config_pull_mark_failure(todo_snapshot, "sync oom");
		return;
	}

	*next_snapshot = *todo_snapshot;
	app_device_config_snapshot_t next_config = *device_config_snapshot;
	s_config_in_progress = true;
	todo_snapshot->sync_in_progress = true;

	int status = 0;
	int ret = sync_transport_http_request("GET", APP_DEVICE_CONFIG_WEB_PATH, NULL, body, BODY_CAP, &status);
	app_settings_t current_settings = { 0 };
	settings_model_get(&current_settings);
	if (ret == 0 && status == 200 &&
	    sync_protocol_parse_device_config(body, &current_settings, &next_config, next_snapshot)) {
		next_snapshot->sync_ok = true;
		next_snapshot->sync_in_progress = false;
		next_snapshot->last_error[0] = '\0';
		*todo_snapshot = *next_snapshot;

		*device_config_snapshot = next_config;
		if (memcmp(&current_settings, &device_config_snapshot->settings, sizeof(current_settings)) != 0) {
			(void)settings_model_set(&device_config_snapshot->settings);
			app_bus_event_t updated = {
				.type = APP_BUS_EVENT_WEB_CONFIG_UPDATED,
			};
			updated.data.settings.settings = device_config_snapshot->settings;
			(void)app_bus_publish(&updated);
		}
		sync_config_pull_touch_last_sync(todo_snapshot);
		sync_todo_cache_save(todo_snapshot);
		s_config_failures = 0;
		s_config_backoff_until_us = 0;
		ESP_LOGI(TAG, "config pull ok count=%u cfg=%u bytes=%u",
			 (unsigned)todo_snapshot->count, (unsigned)device_config_snapshot->config_version,
			 (unsigned)strlen(body));
	} else {
		char reason[64];
		(void)snprintf(reason, sizeof(reason), "config pull err=%d status=%d", ret, status);
		sync_config_pull_mark_failure(todo_snapshot, reason);
		ESP_LOGW(TAG, "%s heap=%u", reason, (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
	}

	free(next_snapshot);
	free(body);
	todo_snapshot->sync_in_progress = false;
	s_config_in_progress = false;
}
