#include "app_module.h"
#include <app/app_config.h>
#include <app/module_common.h>
#include <app/net_service.h>
#include <app/settings_model.h>
#include <app/environment_service.h>
#include <app/presence_service.h>

#include <ctype.h>
#include <esp_err.h>
#include <esp_event.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <lwip/apps/sntp.h>
#include <lwip/inet.h>
#include <nvs.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "net";
static bool s_event_loop_owned;
static bool s_wifi_initialized;
static esp_netif_t *s_wifi_sta_netif;
static esp_event_handler_instance_t s_wifi_event_instance;
static esp_event_handler_instance_t s_ip_event_instance;
static bool s_wifi_event_registered;
static bool s_ip_event_registered;
static app_net_status_t s_status;
static app_todo_snapshot_t s_todo_snapshot;
static esp_timer_handle_t s_reconnect_timer;
static bool s_reconnect_timer_running;
static esp_timer_handle_t s_sntp_sync_timer;
static bool s_sntp_sync_timer_running;
static esp_timer_handle_t s_todo_sync_timer;
static bool s_todo_sync_timer_running;
static esp_timer_handle_t s_status_report_timer;
static bool s_status_report_timer_running;
static bool s_todo_sync_in_progress;
static int64_t s_last_todo_auto_sync_us;
static int64_t s_last_status_report_us;
static QueueHandle_t s_todo_request_queue;
static TaskHandle_t s_todo_task;
static app_device_config_snapshot_t s_device_config_snapshot;

#define APP_TODO_CACHE_NAMESPACE "todo_cache"
#define APP_TODO_CACHE_BLOB_KEY "snapshot_v1"
#define APP_TODO_CACHE_MAGIC 0x544F444FU
#define APP_TODO_CACHE_VERSION 1U

typedef enum {
	NET_TODO_REQUEST_SYNC = 1,
	NET_TODO_REQUEST_SET_DONE,
	NET_TODO_REQUEST_DELETE,
	NET_TODO_REQUEST_REPORT_EVENT,
	NET_TODO_REQUEST_REPORT_STATUS,
	NET_TODO_REQUEST_PUSH_ALARMS,
	NET_TODO_REQUEST_PUSH_VOICE_SETTINGS,
} net_todo_request_t;

typedef struct {
	char *buffer;
	size_t len;
	size_t cap;
} net_http_buffer_t;

typedef struct {
	net_todo_request_t type;
	char todo_id[24];
	char event_type[32];
	bool done;
	app_settings_t settings;
} net_todo_request_msg_t;

typedef struct {
	uint32_t magic;
	uint16_t version;
	uint16_t size;
	app_todo_snapshot_t snapshot;
} net_todo_cache_blob_t;

static int net_service_report_event_once(const char *event_type, const char *todo_id);
static int net_service_push_alarm_settings_once(const app_settings_t *settings);
static int net_service_push_voice_settings_once(const app_settings_t *settings);

static void net_service_configure_timezone(void)
{
	setenv("TZ", "CST-8", 1);
	tzset();
	ESP_LOGI(TAG, "timezone set to %s", "CST-8");
}

static void net_service_start_sntp(void)
{
	if (s_status.time_synced) {
		return;
	}

	sntp_stop();
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
	sntp_setservername(0, APP_SNTP_SERVER);
	sntp_init();
	ESP_LOGI(TAG, "SNTP start server=%s", APP_SNTP_SERVER);
}

static void net_service_try_mark_time_synced(void)
{
	time_t now = 0;
	struct tm timeinfo = { 0 };

	time(&now);
	localtime_r(&now, &timeinfo);
	if (timeinfo.tm_year > (2016 - 1900)) {
		s_status.time_synced = true;
	}
}

static void net_service_stop_sntp_sync_timer(void)
{
	if (s_sntp_sync_timer != NULL && s_sntp_sync_timer_running) {
		(void)esp_timer_stop(s_sntp_sync_timer);
		s_sntp_sync_timer_running = false;
	}
}

static void net_service_start_sntp_sync_timer(void)
{
	if (s_sntp_sync_timer == NULL || s_sntp_sync_timer_running) {
		return;
	}
	if (esp_timer_start_periodic(s_sntp_sync_timer, 120000000ULL) == ESP_OK) {
		s_sntp_sync_timer_running = true;
	}
}

static void net_service_reset_todo_snapshot(void)
{
	s_todo_snapshot = (app_todo_snapshot_t){ 0 };
}

static void net_service_reset_device_config_snapshot(void)
{
	app_settings_t defaults = { 0 };
	settings_model_defaults(&defaults);
	s_device_config_snapshot = (app_device_config_snapshot_t){
		.config_version = 0,
		.settings = defaults,
	};
}

static void net_service_cache_sanitize_snapshot(app_todo_snapshot_t *snapshot)
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

static void net_service_cache_save(void)
{
	net_todo_cache_blob_t blob = {
		.magic = APP_TODO_CACHE_MAGIC,
		.version = APP_TODO_CACHE_VERSION,
		.size = sizeof(blob.snapshot),
		.snapshot = s_todo_snapshot,
	};
	net_service_cache_sanitize_snapshot(&blob.snapshot);

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
		return;
	}

	ESP_LOGI(TAG, "todo cache saved count=%u sync_ok=%d", (unsigned)blob.snapshot.count,
		 blob.snapshot.sync_ok ? 1 : 0);
}

static bool net_service_cache_load(void)
{
	nvs_handle_t handle = 0;
	esp_err_t err = nvs_open(APP_TODO_CACHE_NAMESPACE, NVS_READONLY, &handle);
	if (err == ESP_ERR_NVS_NOT_FOUND) {
		ESP_LOGI(TAG, "todo cache missing");
		return false;
	}
	if (err != ESP_OK) {
		ESP_LOGW(TAG, "todo cache open read failed: %s", esp_err_to_name(err));
		return false;
	}

	net_todo_cache_blob_t blob = { 0 };
	size_t size = sizeof(blob);
	err = nvs_get_blob(handle, APP_TODO_CACHE_BLOB_KEY, &blob, &size);
	nvs_close(handle);
	if (err != ESP_OK) {
		ESP_LOGI(TAG, "todo cache blob missing: %s", esp_err_to_name(err));
		return false;
	}
	if (size != sizeof(blob) || blob.magic != APP_TODO_CACHE_MAGIC || blob.version != APP_TODO_CACHE_VERSION ||
	    blob.size != sizeof(blob.snapshot)) {
		ESP_LOGW(TAG, "todo cache incompatible size=%u magic=0x%08x version=%u", (unsigned)size,
			 (unsigned)blob.magic, (unsigned)blob.version);
		return false;
	}

	net_service_cache_sanitize_snapshot(&blob.snapshot);
	s_todo_snapshot = blob.snapshot;
	ESP_LOGI(TAG, "todo cache loaded count=%u sync_ok=%d last_sync=%s", (unsigned)s_todo_snapshot.count,
		 s_todo_snapshot.sync_ok ? 1 : 0,
		 s_todo_snapshot.last_sync_at[0] != '\0' ? s_todo_snapshot.last_sync_at : "--");
	return true;
}

static void net_service_stop_reconnect_timer(void)
{
	if (s_reconnect_timer != NULL && s_reconnect_timer_running) {
		(void)esp_timer_stop(s_reconnect_timer);
		s_reconnect_timer_running = false;
	}
}

static void net_service_start_reconnect_timer(void)
{
	if (s_reconnect_timer == NULL || s_reconnect_timer_running || strlen(APP_WIFI_STA_SSID) == 0U) {
		return;
	}

	if (esp_timer_start_periodic(s_reconnect_timer, 10000000ULL) == ESP_OK) {
		s_reconnect_timer_running = true;
	}
}

static void net_service_stop_todo_sync_timer(void)
{
	if (s_todo_sync_timer != NULL && s_todo_sync_timer_running) {
		(void)esp_timer_stop(s_todo_sync_timer);
		s_todo_sync_timer_running = false;
	}
}

static void net_service_stop_status_report_timer(void)
{
	if (s_status_report_timer != NULL && s_status_report_timer_running) {
		(void)esp_timer_stop(s_status_report_timer);
		s_status_report_timer_running = false;
	}
}

static void net_service_start_todo_sync_timer(void)
{
	if (s_todo_sync_timer == NULL || s_todo_sync_timer_running) {
		return;
	}
	if (esp_timer_start_periodic(s_todo_sync_timer, 60000000ULL) == ESP_OK) {
		s_todo_sync_timer_running = true;
	}
}

static void net_service_start_status_report_timer(void)
{
	if (s_status_report_timer == NULL || s_status_report_timer_running) {
		return;
	}
	if (esp_timer_start_periodic(s_status_report_timer, 10000000ULL) == ESP_OK) {
		s_status_report_timer_running = true;
	}
}

static void net_service_reconnect_cb(void *arg)
{
	(void)arg;

	if (!s_wifi_initialized || s_status.wifi_connected || strlen(APP_WIFI_STA_SSID) == 0U) {
		return;
	}

	ESP_LOGI(TAG, "wifi retry connect to %s", APP_WIFI_STA_SSID);
	(void)esp_wifi_connect();
}

static void net_service_sntp_sync_cb(void *arg)
{
	(void)arg;

	if (!s_status.wifi_connected || !s_status.ip_ready) {
		return;
	}

	s_status.time_synced = false;
	net_service_start_sntp();
	net_service_try_mark_time_synced();
	ESP_LOGI(TAG, "SNTP sync trigger");
}

static esp_err_t net_http_event_handler(esp_http_client_event_t *evt)
{
	net_http_buffer_t *ctx = (net_http_buffer_t *)evt->user_data;
	if (ctx == NULL) {
		return ESP_OK;
	}
	if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data != NULL && evt->data_len > 0) {
		if ((ctx->len + (size_t)evt->data_len + 1U) > ctx->cap) {
			return ESP_FAIL;
		}
		memcpy(ctx->buffer + ctx->len, evt->data, (size_t)evt->data_len);
		ctx->len += (size_t)evt->data_len;
		ctx->buffer[ctx->len] = '\0';
	}
	return ESP_OK;
}

static const char *net_find_json_key(const char *json, const char *key)
{
	static char pattern[32];
	(void)snprintf(pattern, sizeof(pattern), "\"%s\"", key);
	return strstr(json, pattern);
}

static bool net_parse_json_string(const char *json, const char *key, char *out, size_t out_size)
{
	const char *p = net_find_json_key(json, key);
	if (p == NULL) {
		return false;
	}
	p = strchr(p, ':');
	if (p == NULL) {
		return false;
	}
	p++;
	while (*p != '\0' && isspace((unsigned char)*p)) {
		p++;
	}
	if (*p != '\"') {
		return false;
	}
	p++;
	const char *end = strchr(p, '\"');
	if (end == NULL) {
		return false;
	}
	size_t len = (size_t)(end - p);
	if (len >= out_size) {
		len = out_size - 1U;
	}
	memcpy(out, p, len);
	out[len] = '\0';
	return true;
}

static bool net_parse_json_bool(const char *json, const char *key, bool *out)
{
	const char *p = net_find_json_key(json, key);
	if (p == NULL) {
		return false;
	}
	p = strchr(p, ':');
	if (p == NULL) {
		return false;
	}
	p++;
	while (*p != '\0' && isspace((unsigned char)*p)) {
		p++;
	}
	if (strncmp(p, "true", 4) == 0) {
		*out = true;
		return true;
	}
	if (strncmp(p, "false", 5) == 0) {
		*out = false;
		return true;
	}
	return false;
}

static bool net_parse_todo_items(const char *json, app_todo_snapshot_t *snapshot)
{
	const char *items = strstr(json, "\"todos_active\"");
	if (items == NULL) {
		items = strstr(json, "\"items\"");
	}
	if (items == NULL) {
		return false;
	}
	const char *p = strchr(items, '[');
	if (p == NULL) {
		return false;
	}
	p++;
	snapshot->count = 0;

	while (*p != '\0' && snapshot->count < APP_TODO_MAX_ITEMS) {
		const char *obj_start = strchr(p, '{');
		if (obj_start == NULL) {
			break;
		}
		const char *obj_end = strchr(obj_start, '}');
		if (obj_end == NULL) {
			return false;
		}
		char obj_buf[256];
		size_t len = (size_t)(obj_end - obj_start + 1U);
		if (len >= sizeof(obj_buf)) {
			len = sizeof(obj_buf) - 1U;
		}
		memcpy(obj_buf, obj_start, len);
		obj_buf[len] = '\0';

		app_todo_item_t *item = &snapshot->items[snapshot->count];
		if (!net_parse_json_string(obj_buf, "id", item->id, sizeof(item->id))) {
			break;
		}
		if (!net_parse_json_string(obj_buf, "text", item->text, sizeof(item->text))) {
			break;
		}
		(void)net_parse_json_bool(obj_buf, "done", &item->done);
		(void)net_parse_json_string(obj_buf, "updated_at", item->updated_at, sizeof(item->updated_at));
		snapshot->count++;
		p = obj_end + 1;
	}

	return true;
}

static bool net_parse_json_uint32(const char *json, const char *key, uint32_t *out)
{
	const char *p = net_find_json_key(json, key);
	if (p == NULL || out == NULL) {
		return false;
	}
	p = strchr(p, ':');
	if (p == NULL) {
		return false;
	}
	p++;
	while (*p != '\0' && isspace((unsigned char)*p)) {
		p++;
	}
	char *end = NULL;
	unsigned long value = strtoul(p, &end, 10);
	if (end == p) {
		return false;
	}
	*out = (uint32_t)value;
	return true;
}

static bool net_parse_voice_settings(const char *json, app_settings_t *settings)
{
	const char *voice = strstr(json, "\"voice_settings\"");
	if (voice == NULL || settings == NULL) {
		return false;
	}
	const char *obj_start = strchr(voice, '{');
	const char *obj_end = obj_start != NULL ? strchr(obj_start, '}') : NULL;
	if (obj_start == NULL || obj_end == NULL) {
		return false;
	}
	char obj_buf[256];
	size_t len = (size_t)(obj_end - obj_start + 1U);
	if (len >= sizeof(obj_buf)) {
		len = sizeof(obj_buf) - 1U;
	}
	memcpy(obj_buf, obj_start, len);
	obj_buf[len] = '\0';
	(void)net_parse_json_bool(obj_buf, "todo_voice_on", &settings->todo_voice_on);
	(void)net_parse_json_bool(obj_buf, "alarm_voice_on", &settings->alarm_voice_on);
	(void)net_parse_json_bool(obj_buf, "env_voice_on", &settings->env_voice_on);
	(void)net_parse_json_bool(obj_buf, "env_alert_on", &settings->env_alert_on);
	(void)net_parse_json_bool(obj_buf, "home_hour_chime_on", &settings->home_hour_chime_on);
	return true;
}

static bool net_parse_alarm_items(const char *json, app_settings_t *settings)
{
	const char *alarms = strstr(json, "\"alarms\"");
	if (alarms == NULL || settings == NULL) {
		return false;
	}
	const char *p = strchr(alarms, '[');
	if (p == NULL) {
		return false;
	}
	p++;
	settings->alarm_count = 0;

	while (*p != '\0' && settings->alarm_count < APP_SETTINGS_MAX_ALARMS) {
		const char *obj_start = strchr(p, '{');
		if (obj_start == NULL) {
			break;
		}
		const char *obj_end = strchr(obj_start, '}');
		if (obj_end == NULL) {
			return false;
		}
		char obj_buf[256];
		size_t len = (size_t)(obj_end - obj_start + 1U);
		if (len >= sizeof(obj_buf)) {
			len = sizeof(obj_buf) - 1U;
		}
		memcpy(obj_buf, obj_start, len);
		obj_buf[len] = '\0';

		app_alarm_setting_t *alarm = &settings->alarms[settings->alarm_count];
		uint32_t hour = 0;
		uint32_t minute = 0;
		if (!net_parse_json_uint32(obj_buf, "hour", &hour) || !net_parse_json_uint32(obj_buf, "minute", &minute)) {
			break;
		}
		alarm->hour = (uint8_t)(hour % 24U);
		alarm->minute = (uint8_t)(minute % 60U);
		(void)net_parse_json_bool(obj_buf, "repeat", &alarm->repeat);
		(void)net_parse_json_bool(obj_buf, "enabled", &alarm->enabled);
		(void)net_parse_json_bool(obj_buf, "voice", &alarm->voice);
		settings->alarm_count++;
		p = obj_end + 1;
	}

	return true;
}

static bool net_parse_device_config(const char *json, app_device_config_snapshot_t *snapshot,
					   app_todo_snapshot_t *todo_snapshot)
{
	if (json == NULL || snapshot == NULL || todo_snapshot == NULL) {
		return false;
	}

	app_settings_t settings = { 0 };
	settings_model_get(&settings);
	(void)net_parse_json_uint32(json, "config_version", &snapshot->config_version);
	(void)net_parse_json_string(json, "updated_at", snapshot->updated_at, sizeof(snapshot->updated_at));
	(void)net_parse_voice_settings(json, &settings);
	(void)net_parse_alarm_items(json, &settings);
	if (!net_parse_todo_items(json, todo_snapshot)) {
		return false;
	}
	snapshot->settings = settings;
	return true;
}

static int net_find_todo_index_by_id(const char *todo_id)
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

static void net_touch_todo_last_sync(void)
{
	time_t now = 0;
	struct tm timeinfo = { 0 };

	time(&now);
	localtime_r(&now, &timeinfo);
	(void)snprintf(s_todo_snapshot.last_sync_at, sizeof(s_todo_snapshot.last_sync_at), "%02d:%02d:%02d",
		       timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

static void net_format_now_iso(char *out, size_t out_size)
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

static esp_http_client_handle_t net_http_client_open(const char *url, net_http_buffer_t *buffer)
{
	esp_http_client_config_t cfg = {
		.url = url,
		.timeout_ms = 5000,
		.event_handler = net_http_event_handler,
		.user_data = buffer,
	};
	return esp_http_client_init(&cfg);
}

static void net_service_sync_todos_once(void)
{
	if (!s_status.wifi_connected || !s_status.ip_ready) {
		s_todo_snapshot.sync_ok = false;
		s_todo_snapshot.sync_in_progress = false;
		strlcpy(s_todo_snapshot.last_error, "wifi offline", sizeof(s_todo_snapshot.last_error));
		return;
	}
	if (s_todo_sync_in_progress) {
		return;
	}

	static const size_t TODO_HTTP_BODY_CAP = 2048U;
	char url[160];
	char *body = (char *)calloc(1, TODO_HTTP_BODY_CAP);
	app_todo_snapshot_t *next_snapshot = (app_todo_snapshot_t *)malloc(sizeof(*next_snapshot));
	if (body == NULL || next_snapshot == NULL) {
		free(body);
		free(next_snapshot);
		s_todo_snapshot.sync_ok = false;
		s_todo_snapshot.sync_in_progress = false;
		strlcpy(s_todo_snapshot.last_error, "sync oom", sizeof(s_todo_snapshot.last_error));
		ESP_LOGE(TAG, "todo sync oom heap=%u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
		return;
	}

	net_http_buffer_t buffer = {
		.buffer = body,
		.len = 0,
		.cap = TODO_HTTP_BODY_CAP,
	};
	*next_snapshot = s_todo_snapshot;
	s_todo_sync_in_progress = true;
	s_todo_snapshot.sync_in_progress = true;
	app_device_config_snapshot_t next_config = s_device_config_snapshot;
	(void)snprintf(url, sizeof(url), "http://%s:%d%s", APP_TODO_WEB_HOST, APP_TODO_WEB_PORT, APP_DEVICE_CONFIG_WEB_PATH);
	ESP_LOGI(TAG, "config sync start url=%s stack_hwm=%u heap=%u", url,
		 (unsigned)uxTaskGetStackHighWaterMark(NULL), (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));

	esp_http_client_handle_t client = net_http_client_open(url, &buffer);
	if (client == NULL) {
		strlcpy(s_todo_snapshot.last_error, "client init failed", sizeof(s_todo_snapshot.last_error));
		s_todo_snapshot.sync_in_progress = false;
		s_todo_sync_in_progress = false;
		free(next_snapshot);
		free(body);
		return;
	}

	esp_err_t err = esp_http_client_perform(client);
	int status = esp_http_client_get_status_code(client);
	if (err == ESP_OK && status == 200 && net_parse_device_config(body, &next_config, next_snapshot)) {
		next_snapshot->sync_ok = true;
		next_snapshot->sync_in_progress = false;
		next_snapshot->last_error[0] = '\0';
		time_t now = 0;
		struct tm timeinfo = { 0 };
		time(&now);
		localtime_r(&now, &timeinfo);
		(void)snprintf(next_snapshot->last_sync_at, sizeof(next_snapshot->last_sync_at), "%02d:%02d:%02d",
			       timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
			s_todo_snapshot = *next_snapshot;
			app_settings_t current_settings = { 0 };
			settings_model_get(&current_settings);
			s_device_config_snapshot = next_config;
			if (memcmp(&current_settings, &s_device_config_snapshot.settings, sizeof(current_settings)) != 0) {
				(void)settings_model_set(&s_device_config_snapshot.settings);
			}
		net_service_cache_save();
		ESP_LOGI(TAG, "config sync ok count=%u cfg=%u bytes=%u stack_hwm=%u heap=%u",
			 (unsigned)s_todo_snapshot.count, (unsigned)buffer.len,
			 (unsigned)s_device_config_snapshot.config_version,
			 (unsigned)uxTaskGetStackHighWaterMark(NULL), (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
	} else {
		s_todo_snapshot.sync_ok = false;
		s_todo_snapshot.sync_in_progress = false;
		(void)snprintf(s_todo_snapshot.last_error, sizeof(s_todo_snapshot.last_error), "http err=%d status=%d",
			       (int)err, status);
		ESP_LOGW(TAG, "config sync failed err=%d status=%d bytes=%u stack_hwm=%u heap=%u",
			 (int)err, status, (unsigned)buffer.len, (unsigned)uxTaskGetStackHighWaterMark(NULL),
			 (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
	}

	esp_http_client_cleanup(client);
	free(next_snapshot);
	free(body);
	s_todo_snapshot.sync_in_progress = false;
	s_todo_sync_in_progress = false;
}

static int net_service_update_todo_done_once(const char *todo_id, bool done)
{
	if (todo_id == NULL || todo_id[0] == '\0') {
		return -1;
	}
	if (!s_status.wifi_connected || !s_status.ip_ready) {
		strlcpy(s_todo_snapshot.last_error, "wifi offline", sizeof(s_todo_snapshot.last_error));
		return -1;
	}

	char url[192];
	char response[256] = { 0 };
	net_http_buffer_t buffer = {
		.buffer = response,
		.len = 0,
		.cap = sizeof(response),
	};
	(void)snprintf(url, sizeof(url), "http://%s:%d%s/%s/complete", APP_TODO_WEB_HOST, APP_TODO_WEB_PORT,
		       APP_TODO_WEB_PATH, todo_id);
	ESP_LOGI(TAG, "todo set done start id=%s done=%d", todo_id, done ? 1 : 0);

	esp_http_client_handle_t client = net_http_client_open(url, &buffer);
	if (client == NULL) {
		strlcpy(s_todo_snapshot.last_error, "client init failed", sizeof(s_todo_snapshot.last_error));
		return -1;
	}

	esp_http_client_set_method(client, HTTP_METHOD_POST);

	esp_err_t err = esp_http_client_perform(client);
	const int status = esp_http_client_get_status_code(client);
	esp_http_client_cleanup(client);
	if (err != ESP_OK || status != 200) {
		(void)snprintf(s_todo_snapshot.last_error, sizeof(s_todo_snapshot.last_error), "todo complete err=%d status=%d",
			       (int)err, status);
		ESP_LOGW(TAG, "todo complete failed id=%s done=%d err=%d status=%d", todo_id, done ? 1 : 0, (int)err,
			 status);
		return -1;
	}

	const int index = net_find_todo_index_by_id(todo_id);
	if (index >= 0) {
		for (uint8_t i = (uint8_t)index; i + 1U < s_todo_snapshot.count; i++) {
			s_todo_snapshot.items[i] = s_todo_snapshot.items[i + 1U];
		}
		if (s_todo_snapshot.count > 0U) {
			s_todo_snapshot.count--;
			s_todo_snapshot.items[s_todo_snapshot.count] = (app_todo_item_t){ 0 };
		}
	}
	s_todo_snapshot.sync_ok = true;
	s_todo_snapshot.last_error[0] = '\0';
	net_touch_todo_last_sync();
	net_service_cache_save();
	ESP_LOGI(TAG, "todo complete ok id=%s done=%d", todo_id, done ? 1 : 0);
	(void)net_service_report_event_once("todo_completed", todo_id);
	return 0;
}

static int net_service_delete_todo_once(const char *todo_id)
{
	if (todo_id == NULL || todo_id[0] == '\0') {
		return -1;
	}
	if (!s_status.wifi_connected || !s_status.ip_ready) {
		strlcpy(s_todo_snapshot.last_error, "wifi offline", sizeof(s_todo_snapshot.last_error));
		return -1;
	}

	char url[192];
	char response[64] = { 0 };
	net_http_buffer_t buffer = {
		.buffer = response,
		.len = 0,
		.cap = sizeof(response),
	};
	(void)snprintf(url, sizeof(url), "http://%s:%d%s/%s", APP_TODO_WEB_HOST, APP_TODO_WEB_PORT, APP_TODO_WEB_PATH,
		       todo_id);
	ESP_LOGI(TAG, "todo delete start id=%s", todo_id);

	esp_http_client_handle_t client = net_http_client_open(url, &buffer);
	if (client == NULL) {
		strlcpy(s_todo_snapshot.last_error, "client init failed", sizeof(s_todo_snapshot.last_error));
		return -1;
	}

	esp_http_client_set_method(client, HTTP_METHOD_DELETE);
	esp_err_t err = esp_http_client_perform(client);
	const int status = esp_http_client_get_status_code(client);
	esp_http_client_cleanup(client);
	if (err != ESP_OK || status != 204) {
		(void)snprintf(s_todo_snapshot.last_error, sizeof(s_todo_snapshot.last_error), "todo del err=%d status=%d",
			       (int)err, status);
		ESP_LOGW(TAG, "todo delete failed id=%s err=%d status=%d", todo_id, (int)err, status);
		return -1;
	}

	const int index = net_find_todo_index_by_id(todo_id);
	if (index >= 0) {
		for (uint8_t i = (uint8_t)index; i + 1U < s_todo_snapshot.count; i++) {
			s_todo_snapshot.items[i] = s_todo_snapshot.items[i + 1U];
		}
		if (s_todo_snapshot.count > 0U) {
			s_todo_snapshot.count--;
			s_todo_snapshot.items[s_todo_snapshot.count] = (app_todo_item_t){ 0 };
		}
	}
	s_todo_snapshot.sync_ok = true;
	s_todo_snapshot.last_error[0] = '\0';
	net_touch_todo_last_sync();
	net_service_cache_save();
	ESP_LOGI(TAG, "todo delete ok id=%s", todo_id);
	(void)net_service_report_event_once("todo_deleted", todo_id);
	return 0;
}

static int net_service_report_status_once(void)
{
	if (!s_status.wifi_connected || !s_status.ip_ready) {
		return -1;
	}

	app_environment_snapshot_t env = { 0 };
	app_presence_status_t presence = { 0 };
	char sent_at[40] = { 0 };
	(void)environment_service_get_snapshot(&env);
	(void)presence_service_get_status(&presence);
	net_format_now_iso(sent_at, sizeof(sent_at));

	char url[192];
	char body[256];
	char response[64] = { 0 };
	net_http_buffer_t buffer = {
		.buffer = response,
		.len = 0,
		.cap = sizeof(response),
	};
	(void)snprintf(url, sizeof(url), "http://%s:%d%s", APP_TODO_WEB_HOST, APP_TODO_WEB_PORT, APP_DEVICE_STATUS_WEB_PATH);
	(void)snprintf(body, sizeof(body),
		       "{\"device_id\":\"clock-001\",\"sent_at\":\"%s\",\"online\":true,\"temperature_c\":%.1f,"
		       "\"humidity_percent\":%.1f,\"lux\":%.1f,\"presence_detected\":%s}",
		       sent_at,
		       env.temperature_c, env.humidity_percent, env.lux, presence.detected ? "true" : "false");

	esp_http_client_handle_t client = net_http_client_open(url, &buffer);
	if (client == NULL) {
		return -1;
	}
	esp_http_client_set_method(client, HTTP_METHOD_POST);
	esp_http_client_set_header(client, "Content-Type", "application/json");
	esp_http_client_set_post_field(client, body, (int)strlen(body));
	esp_err_t err = esp_http_client_perform(client);
	const int status = esp_http_client_get_status_code(client);
	esp_http_client_cleanup(client);
	if (err != ESP_OK || status != 200) {
		ESP_LOGW(TAG, "status report failed err=%d status=%d", (int)err, status);
		return -1;
	}
	ESP_LOGI(TAG, "status report ok");
	return 0;
}

static int net_service_report_event_once(const char *event_type, const char *todo_id)
{
	if (event_type == NULL || event_type[0] == '\0' || !s_status.wifi_connected || !s_status.ip_ready) {
		return -1;
	}

	app_environment_snapshot_t env = { 0 };
	app_presence_status_t presence = { 0 };
	char event_at[40] = { 0 };
	(void)environment_service_get_snapshot(&env);
	(void)presence_service_get_status(&presence);
	net_format_now_iso(event_at, sizeof(event_at));

	char url[192];
	char body[320];
	char response[64] = { 0 };
	net_http_buffer_t buffer = {
		.buffer = response,
		.len = 0,
		.cap = sizeof(response),
	};
	(void)snprintf(url, sizeof(url), "http://%s:%d%s", APP_TODO_WEB_HOST, APP_TODO_WEB_PORT, APP_DEVICE_EVENTS_WEB_PATH);
	if (todo_id != NULL && todo_id[0] != '\0') {
		(void)snprintf(body, sizeof(body),
			       "{\"device_id\":\"clock-001\",\"event_at\":\"%s\",\"event_type\":\"%s\",\"todo_id\":\"%s\","
			       "\"temperature_c\":%.1f,\"humidity_percent\":%.1f,\"lux\":%.1f,\"presence_detected\":%s}",
			       event_at,
			       event_type, todo_id, env.temperature_c, env.humidity_percent, env.lux,
			       presence.detected ? "true" : "false");
	} else {
		(void)snprintf(body, sizeof(body),
			       "{\"device_id\":\"clock-001\",\"event_at\":\"%s\",\"event_type\":\"%s\","
			       "\"temperature_c\":%.1f,\"humidity_percent\":%.1f,\"lux\":%.1f,\"presence_detected\":%s}",
			       event_at,
			       event_type, env.temperature_c, env.humidity_percent, env.lux,
			       presence.detected ? "true" : "false");
	}

	esp_http_client_handle_t client = net_http_client_open(url, &buffer);
	if (client == NULL) {
		return -1;
	}
	esp_http_client_set_method(client, HTTP_METHOD_POST);
	esp_http_client_set_header(client, "Content-Type", "application/json");
	esp_http_client_set_post_field(client, body, (int)strlen(body));
	esp_err_t err = esp_http_client_perform(client);
	const int status = esp_http_client_get_status_code(client);
	esp_http_client_cleanup(client);
	if (err != ESP_OK || status != 200) {
		ESP_LOGW(TAG, "event report failed type=%s err=%d status=%d", event_type, (int)err, status);
		return -1;
	}
	ESP_LOGI(TAG, "event report ok type=%s", event_type);
	return 0;
}

static int net_service_push_alarm_settings_once(const app_settings_t *settings)
{
	if (settings == NULL || !s_status.wifi_connected || !s_status.ip_ready) {
		return -1;
	}

	char url[192];
	char body[768];
	char response[128] = { 0 };
	net_http_buffer_t buffer = {
		.buffer = response,
		.len = 0,
		.cap = sizeof(response),
	};

	size_t used = (size_t)snprintf(body, sizeof(body), "{\"config_version\":%u,\"alarms\":[",
				      (unsigned)s_device_config_snapshot.config_version);
	for (uint8_t i = 0; i < settings->alarm_count && i < APP_SETTINGS_MAX_ALARMS; i++) {
		const app_alarm_setting_t *alarm = &settings->alarms[i];
		used += (size_t)snprintf(body + used, sizeof(body) - used,
					 "%s{\"hour\":%u,\"minute\":%u,\"repeat\":%s,\"enabled\":%s,\"voice\":%s}",
					 i == 0U ? "" : ",", (unsigned)alarm->hour, (unsigned)alarm->minute,
					 alarm->repeat ? "true" : "false", alarm->enabled ? "true" : "false",
					 alarm->voice ? "true" : "false");
		if (used >= sizeof(body)) {
			return -1;
		}
	}
	used += (size_t)snprintf(body + used, sizeof(body) - used, "]}");
	if (used >= sizeof(body)) {
		return -1;
	}

	(void)snprintf(url, sizeof(url), "http://%s:%d/api/device/alarms", APP_TODO_WEB_HOST, APP_TODO_WEB_PORT);
	esp_http_client_handle_t client = net_http_client_open(url, &buffer);
	if (client == NULL) {
		return -1;
	}
	esp_http_client_set_method(client, HTTP_METHOD_PUT);
	esp_http_client_set_header(client, "Content-Type", "application/json");
	esp_http_client_set_post_field(client, body, (int)strlen(body));
	esp_err_t err = esp_http_client_perform(client);
	const int status = esp_http_client_get_status_code(client);
	esp_http_client_cleanup(client);
	if (err != ESP_OK || status != 200) {
		ESP_LOGW(TAG, "push alarms failed err=%d status=%d", (int)err, status);
		return -1;
	}
	ESP_LOGI(TAG, "push alarms ok count=%u", (unsigned)settings->alarm_count);
	return 0;
}

static int net_service_push_voice_settings_once(const app_settings_t *settings)
{
	if (settings == NULL || !s_status.wifi_connected || !s_status.ip_ready) {
		return -1;
	}

	char url[192];
	char body[256];
	char response[128] = { 0 };
	net_http_buffer_t buffer = {
		.buffer = response,
		.len = 0,
		.cap = sizeof(response),
	};

	(void)snprintf(body, sizeof(body),
		       "{\"config_version\":%u,\"voice_settings\":{\"todo_voice_on\":%s,\"alarm_voice_on\":%s,"
		       "\"env_voice_on\":%s,\"env_alert_on\":%s,\"home_hour_chime_on\":%s}}",
		       (unsigned)s_device_config_snapshot.config_version,
		       settings->todo_voice_on ? "true" : "false",
		       settings->alarm_voice_on ? "true" : "false",
		       settings->env_voice_on ? "true" : "false",
		       settings->env_alert_on ? "true" : "false",
		       settings->home_hour_chime_on ? "true" : "false");

	(void)snprintf(url, sizeof(url), "http://%s:%d/api/device/voice-settings", APP_TODO_WEB_HOST,
		       APP_TODO_WEB_PORT);
	esp_http_client_handle_t client = net_http_client_open(url, &buffer);
	if (client == NULL) {
		return -1;
	}
	esp_http_client_set_method(client, HTTP_METHOD_PUT);
	esp_http_client_set_header(client, "Content-Type", "application/json");
	esp_http_client_set_post_field(client, body, (int)strlen(body));
	esp_err_t err = esp_http_client_perform(client);
	const int status = esp_http_client_get_status_code(client);
	esp_http_client_cleanup(client);
	if (err != ESP_OK || status != 200) {
		ESP_LOGW(TAG, "push voice settings failed err=%d status=%d", (int)err, status);
		return -1;
	}
	ESP_LOGI(TAG, "push voice settings ok");
	return 0;
}

static int net_service_queue_todo_sync(void)
{
	if (!s_status.wifi_connected || !s_status.ip_ready) {
		s_todo_snapshot.sync_ok = false;
		s_todo_snapshot.sync_in_progress = false;
		strlcpy(s_todo_snapshot.last_error, "wifi offline", sizeof(s_todo_snapshot.last_error));
		return -1;
	}
	if (s_todo_request_queue == NULL) {
		s_todo_snapshot.sync_ok = false;
		s_todo_snapshot.sync_in_progress = false;
		strlcpy(s_todo_snapshot.last_error, "sync queue missing", sizeof(s_todo_snapshot.last_error));
		return -1;
	}
	if (s_todo_sync_in_progress || s_todo_snapshot.sync_in_progress) {
		return 0;
	}

	const net_todo_request_msg_t request = {
		.type = NET_TODO_REQUEST_SYNC,
	};
	s_todo_snapshot.sync_in_progress = true;
	if (xQueueSend(s_todo_request_queue, &request, 0) != pdTRUE) {
		s_todo_snapshot.sync_in_progress = false;
		strlcpy(s_todo_snapshot.last_error, "sync queue full", sizeof(s_todo_snapshot.last_error));
		return -1;
	}

	return 0;
}

static void net_service_todo_task(void *arg)
{
	(void)arg;

	for (;;) {
		net_todo_request_msg_t request = { 0 };
		if (xQueueReceive(s_todo_request_queue, &request, portMAX_DELAY) != pdTRUE) {
			continue;
		}
		if (request.type == NET_TODO_REQUEST_SYNC) {
			net_service_sync_todos_once();
		} else if (request.type == NET_TODO_REQUEST_SET_DONE) {
			(void)net_service_update_todo_done_once(request.todo_id, request.done);
		} else if (request.type == NET_TODO_REQUEST_DELETE) {
			(void)net_service_delete_todo_once(request.todo_id);
		} else if (request.type == NET_TODO_REQUEST_REPORT_EVENT) {
			(void)net_service_report_event_once(request.event_type, request.todo_id);
		} else if (request.type == NET_TODO_REQUEST_REPORT_STATUS) {
			(void)net_service_report_status_once();
		} else if (request.type == NET_TODO_REQUEST_PUSH_ALARMS) {
			(void)net_service_push_alarm_settings_once(&request.settings);
		} else if (request.type == NET_TODO_REQUEST_PUSH_VOICE_SETTINGS) {
			(void)net_service_push_voice_settings_once(&request.settings);
		}
	}
}

static void net_service_todo_sync_cb(void *arg)
{
	(void)arg;

	const int64_t now_us = esp_timer_get_time();
	if (s_last_todo_auto_sync_us > 0 &&
	    (now_us - s_last_todo_auto_sync_us) < (int64_t)APP_CONFIG_SYNC_INTERVAL_S * 1000000LL) {
		return;
	}

	if (net_service_queue_todo_sync() == 0) {
		s_last_todo_auto_sync_us = now_us;
	}
}

static void net_service_status_report_cb(void *arg)
{
	(void)arg;

	const int64_t now_us = esp_timer_get_time();
	if (s_last_status_report_us > 0 &&
	    (now_us - s_last_status_report_us) < (int64_t)APP_STATUS_REPORT_INTERVAL_S * 1000000LL) {
		return;
	}
	if (s_todo_request_queue == NULL) {
		return;
	}
	const net_todo_request_msg_t request = {
		.type = NET_TODO_REQUEST_REPORT_STATUS,
	};
	if (xQueueSend(s_todo_request_queue, &request, 0) == pdTRUE) {
		s_last_status_report_us = now_us;
	}
}

static void net_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
	(void)arg;

	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		s_status.wifi_started = true;
		if (strlen(APP_WIFI_STA_SSID) > 0U) {
			ESP_LOGI(TAG, "wifi start, connecting to %s", APP_WIFI_STA_SSID);
			net_service_start_reconnect_timer();
			(void)esp_wifi_connect();
		} else {
			ESP_LOGI(TAG, "wifi credentials not configured, skip connect");
		}
		return;
	}

	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		const wifi_event_sta_disconnected_t *event = (const wifi_event_sta_disconnected_t *)event_data;
		s_status.wifi_connected = false;
		s_status.ip_ready = false;
		s_status.time_synced = false;
		s_status.connected_ssid[0] = '\0';
		s_status.ip_addr[0] = '\0';
		s_status.rssi = 0;
		s_todo_snapshot.sync_in_progress = false;
		if (strlen(APP_WIFI_STA_SSID) > 0U) {
			ESP_LOGW(TAG, "wifi disconnected, reason=%d retry every 10s",
				 event != NULL ? (int)event->reason : -1);
			net_service_start_reconnect_timer();
		}
		return;
	}

	if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
		s_status.wifi_connected = true;
		s_status.ip_ready = true;
		strlcpy(s_status.connected_ssid, APP_WIFI_STA_SSID, sizeof(s_status.connected_ssid));
		if (event != NULL) {
			(void)snprintf(s_status.ip_addr, sizeof(s_status.ip_addr), IPSTR, IP2STR(&event->ip_info.ip));
		}
		net_service_stop_reconnect_timer();
		net_service_start_sntp();
		net_service_start_sntp_sync_timer();
		net_service_start_todo_sync_timer();
		net_service_start_status_report_timer();
		s_last_todo_auto_sync_us = 0;
		s_last_status_report_us = 0;
		net_service_try_mark_time_synced();
		ESP_LOGI(TAG, "got ip, sntp started and periodic sync enabled");
		return;
	}
}

int net_service_init(void)
{
	net_service_configure_timezone();
	esp_err_t err = esp_netif_init();
	if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
		ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	err = esp_event_loop_create_default();
	if (err == ESP_OK) {
		s_event_loop_owned = true;
	} else if (err != ESP_ERR_INVALID_STATE) {
		ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	s_wifi_sta_netif = esp_netif_create_default_wifi_sta();
	if (s_wifi_sta_netif == NULL) {
		ESP_LOGE(TAG, "esp_netif_create_default_wifi_sta failed");
		err = ESP_FAIL;
		goto fail;
	}

	wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
	wifi_init_cfg.nvs_enable = false;
	err = esp_wifi_init(&wifi_init_cfg);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
		goto fail;
	}
	s_wifi_initialized = true;

	err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &net_event_handler, NULL,
						  &s_wifi_event_instance);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "register WIFI_EVENT failed: %s", esp_err_to_name(err));
		goto fail;
	}
	s_wifi_event_registered = true;

	err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &net_event_handler, NULL,
						  &s_ip_event_instance);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "register IP_EVENT failed: %s", esp_err_to_name(err));
		goto fail;
	}
	s_ip_event_registered = true;

	err = esp_wifi_set_mode(WIFI_MODE_STA);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(err));
		goto fail;
	}

	err = esp_wifi_set_ps(WIFI_PS_NONE);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_wifi_set_ps failed: %s", esp_err_to_name(err));
		goto fail;
	}

	const esp_timer_create_args_t reconnect_timer_args = {
		.callback = &net_service_reconnect_cb,
		.name = "wifi_retry",
	};
	err = esp_timer_create(&reconnect_timer_args, &s_reconnect_timer);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_timer_create failed: %s", esp_err_to_name(err));
		goto fail;
	}

	const esp_timer_create_args_t sntp_timer_args = {
		.callback = &net_service_sntp_sync_cb,
		.name = "sntp_sync",
	};
	err = esp_timer_create(&sntp_timer_args, &s_sntp_sync_timer);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_timer_create sntp failed: %s", esp_err_to_name(err));
		goto fail;
	}

	const esp_timer_create_args_t todo_timer_args = {
		.callback = &net_service_todo_sync_cb,
		.name = "todo_sync",
	};
	err = esp_timer_create(&todo_timer_args, &s_todo_sync_timer);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_timer_create todo failed: %s", esp_err_to_name(err));
		goto fail;
	}

	const esp_timer_create_args_t status_timer_args = {
		.callback = &net_service_status_report_cb,
		.name = "status_report",
	};
	err = esp_timer_create(&status_timer_args, &s_status_report_timer);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_timer_create status failed: %s", esp_err_to_name(err));
		goto fail;
	}

	s_todo_request_queue = xQueueCreate(4, sizeof(net_todo_request_msg_t));
	if (s_todo_request_queue == NULL) {
		ESP_LOGE(TAG, "failed to create todo request queue");
		err = ESP_ERR_NO_MEM;
		goto fail;
	}

	BaseType_t task_ok = xTaskCreate(net_service_todo_task, "todo_sync_task", 6144, NULL, 6, &s_todo_task);
	if (task_ok != pdPASS) {
		ESP_LOGE(TAG, "failed to create todo sync task");
		err = ESP_ERR_NO_MEM;
		goto fail;
	}

	wifi_config_t wifi_config = { 0 };
	if (strlen(APP_WIFI_STA_SSID) > 0U) {
		strlcpy((char *)wifi_config.sta.ssid, APP_WIFI_STA_SSID, sizeof(wifi_config.sta.ssid));
		strlcpy((char *)wifi_config.sta.password, APP_WIFI_STA_PASSWORD, sizeof(wifi_config.sta.password));
		wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
		wifi_config.sta.pmf_cfg.capable = false;
		wifi_config.sta.pmf_cfg.required = false;
		err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(err));
			goto fail;
		}
	}

	s_status = (app_net_status_t){ 0 };
	net_service_reset_todo_snapshot();
	net_service_reset_device_config_snapshot();
	(void)net_service_cache_load();
	ESP_LOGI(TAG, "init");
	return 0;

fail:
	(void)net_service_stop();
	return (int)err;
}

int net_service_start(void)
{
	esp_err_t err = esp_wifi_start();
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	return 0;
}

int net_service_stop(void)
{
	sntp_stop();
	s_status = (app_net_status_t){ 0 };
	net_service_stop_reconnect_timer();
	net_service_stop_sntp_sync_timer();
	net_service_stop_todo_sync_timer();
	net_service_stop_status_report_timer();
	s_todo_sync_in_progress = false;
	s_last_todo_auto_sync_us = 0;
	s_last_status_report_us = 0;
	s_todo_snapshot.sync_in_progress = false;

	if (s_todo_task != NULL) {
		vTaskDelete(s_todo_task);
		s_todo_task = NULL;
	}
	if (s_todo_request_queue != NULL) {
		vQueueDelete(s_todo_request_queue);
		s_todo_request_queue = NULL;
	}

	if (s_ip_event_registered) {
		(void)esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_event_instance);
		s_ip_event_registered = false;
	}
	if (s_wifi_event_registered) {
		(void)esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_event_instance);
		s_wifi_event_registered = false;
	}
	if (s_wifi_initialized) {
		(void)esp_wifi_stop();
		(void)esp_wifi_deinit();
		s_wifi_initialized = false;
	}
	if (s_wifi_sta_netif != NULL) {
		esp_netif_destroy_default_wifi(s_wifi_sta_netif);
		s_wifi_sta_netif = NULL;
	}
	if (s_event_loop_owned) {
		(void)esp_event_loop_delete_default();
		s_event_loop_owned = false;
	}
	if (s_reconnect_timer != NULL) {
		(void)esp_timer_delete(s_reconnect_timer);
		s_reconnect_timer = NULL;
	}
	if (s_sntp_sync_timer != NULL) {
		(void)esp_timer_delete(s_sntp_sync_timer);
		s_sntp_sync_timer = NULL;
	}
	if (s_todo_sync_timer != NULL) {
		(void)esp_timer_delete(s_todo_sync_timer);
		s_todo_sync_timer = NULL;
	}
	if (s_status_report_timer != NULL) {
		(void)esp_timer_delete(s_status_report_timer);
		s_status_report_timer = NULL;
	}

	return 0;
}

bool net_service_get_status(app_net_status_t *out_status)
{
	if (out_status == NULL) {
		return false;
	}

	net_service_try_mark_time_synced();
	*out_status = s_status;
	return true;
}

int net_service_request_connect_now(void)
{
	if (!s_wifi_initialized || !s_status.wifi_started) {
		return -1;
	}

	net_service_stop_reconnect_timer();
	esp_err_t err = esp_wifi_connect();
	if (err != ESP_OK) {
		ESP_LOGW(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
		net_service_start_reconnect_timer();
		return (int)err;
	}

	ESP_LOGI(TAG, "manual connect to %s", APP_WIFI_STA_SSID);
	return 0;
}

bool net_service_get_todo_snapshot(app_todo_snapshot_t *out_snapshot)
{
	if (out_snapshot == NULL) {
		return false;
	}

	*out_snapshot = s_todo_snapshot;
	return true;
}

bool net_service_get_device_config_snapshot(app_device_config_snapshot_t *out_snapshot)
{
	if (out_snapshot == NULL) {
		return false;
	}
	*out_snapshot = s_device_config_snapshot;
	return true;
}

int net_service_request_todo_sync_now(void)
{
	return net_service_queue_todo_sync();
}

int net_service_request_todo_set_done(const char *todo_id, bool done)
{
	if (todo_id == NULL || todo_id[0] == '\0' || s_todo_request_queue == NULL) {
		return -1;
	}

	net_todo_request_msg_t request = {
		.type = NET_TODO_REQUEST_SET_DONE,
		.done = done,
	};
	strlcpy(request.todo_id, todo_id, sizeof(request.todo_id));
	if (xQueueSend(s_todo_request_queue, &request, 0) != pdTRUE) {
		strlcpy(s_todo_snapshot.last_error, "todo op queue full", sizeof(s_todo_snapshot.last_error));
		return -1;
	}
	return 0;
}

int net_service_request_todo_delete(const char *todo_id)
{
	if (todo_id == NULL || todo_id[0] == '\0' || s_todo_request_queue == NULL) {
		return -1;
	}

	net_todo_request_msg_t request = {
		.type = NET_TODO_REQUEST_DELETE,
	};
	strlcpy(request.todo_id, todo_id, sizeof(request.todo_id));
	if (xQueueSend(s_todo_request_queue, &request, 0) != pdTRUE) {
		strlcpy(s_todo_snapshot.last_error, "todo op queue full", sizeof(s_todo_snapshot.last_error));
		return -1;
	}
	return 0;
}

int net_service_request_push_alarm_settings(const app_settings_t *settings)
{
	if (settings == NULL || s_todo_request_queue == NULL) {
		return -1;
	}

	net_todo_request_msg_t request = {
		.type = NET_TODO_REQUEST_PUSH_ALARMS,
		.settings = *settings,
	};
	if (xQueueSend(s_todo_request_queue, &request, 0) != pdTRUE) {
		return -1;
	}
	return 0;
}

int net_service_request_push_voice_settings(const app_settings_t *settings)
{
	if (settings == NULL || s_todo_request_queue == NULL) {
		return -1;
	}

	net_todo_request_msg_t request = {
		.type = NET_TODO_REQUEST_PUSH_VOICE_SETTINGS,
		.settings = *settings,
	};
	if (xQueueSend(s_todo_request_queue, &request, 0) != pdTRUE) {
		return -1;
	}
	return 0;
}

int net_service_request_report_event(const char *event_type, const char *todo_id)
{
	if (event_type == NULL || event_type[0] == '\0' || s_todo_request_queue == NULL) {
		return -1;
	}

	net_todo_request_msg_t request = {
		.type = NET_TODO_REQUEST_REPORT_EVENT,
	};
	strlcpy(request.event_type, event_type, sizeof(request.event_type));
	if (todo_id != NULL) {
		strlcpy(request.todo_id, todo_id, sizeof(request.todo_id));
	}
	if (xQueueSend(s_todo_request_queue, &request, 0) != pdTRUE) {
		return -1;
	}
	return 0;
}
