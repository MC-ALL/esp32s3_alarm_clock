#include "app_module.h"
#include <app/app_config.h>
#include <app/module_common.h>
#include <app/net_service.h>
#include <app/settings_model.h>

#include <ctype.h>
#include <esp_err.h>
#include <esp_event.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <lwip/apps/sntp.h>
#include <lwip/inet.h>
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
static bool s_todo_sync_in_progress;
static int64_t s_last_todo_auto_sync_us;
static QueueHandle_t s_todo_request_queue;
static TaskHandle_t s_todo_task;

typedef enum {
	NET_TODO_REQUEST_SYNC = 1,
} net_todo_request_t;

typedef struct {
	char *buffer;
	size_t len;
	size_t cap;
} net_http_buffer_t;

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

static void net_service_start_todo_sync_timer(void)
{
	if (s_todo_sync_timer == NULL || s_todo_sync_timer_running) {
		return;
	}
	if (esp_timer_start_periodic(s_todo_sync_timer, 60000000ULL) == ESP_OK) {
		s_todo_sync_timer_running = true;
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
	const char *items = strstr(json, "\"items\"");
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

	char url[160];
	char body[2048];
	net_http_buffer_t buffer = {
		.buffer = body,
		.len = 0,
		.cap = sizeof(body),
	};
	app_todo_snapshot_t next_snapshot = s_todo_snapshot;
	s_todo_sync_in_progress = true;
	s_todo_snapshot.sync_in_progress = true;
	(void)snprintf(url, sizeof(url), "http://%s:%d%s", APP_TODO_WEB_HOST, APP_TODO_WEB_PORT, APP_TODO_WEB_PATH);

	esp_http_client_config_t cfg = {
		.url = url,
		.timeout_ms = 5000,
		.event_handler = net_http_event_handler,
		.user_data = &buffer,
	};
	esp_http_client_handle_t client = esp_http_client_init(&cfg);
	if (client == NULL) {
		strlcpy(s_todo_snapshot.last_error, "client init failed", sizeof(s_todo_snapshot.last_error));
		s_todo_snapshot.sync_in_progress = false;
		s_todo_sync_in_progress = false;
		return;
	}

	esp_err_t err = esp_http_client_perform(client);
	int status = esp_http_client_get_status_code(client);
	if (err == ESP_OK && status == 200 && net_parse_todo_items(body, &next_snapshot)) {
		next_snapshot.sync_ok = true;
		next_snapshot.sync_in_progress = false;
		next_snapshot.last_error[0] = '\0';
		time_t now = 0;
		struct tm timeinfo = { 0 };
		time(&now);
		localtime_r(&now, &timeinfo);
		(void)snprintf(next_snapshot.last_sync_at, sizeof(next_snapshot.last_sync_at), "%02d:%02d:%02d",
			       timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
		s_todo_snapshot = next_snapshot;
		ESP_LOGI(TAG, "todo sync ok count=%u", (unsigned)s_todo_snapshot.count);
	} else {
		s_todo_snapshot.sync_ok = false;
		s_todo_snapshot.sync_in_progress = false;
		(void)snprintf(s_todo_snapshot.last_error, sizeof(s_todo_snapshot.last_error), "http err=%d status=%d",
			       (int)err, status);
		ESP_LOGW(TAG, "todo sync failed err=%d status=%d", (int)err, status);
	}

	esp_http_client_cleanup(client);
	s_todo_snapshot.sync_in_progress = false;
	s_todo_sync_in_progress = false;
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

	const net_todo_request_t request = NET_TODO_REQUEST_SYNC;
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
		net_todo_request_t request = 0;
		if (xQueueReceive(s_todo_request_queue, &request, portMAX_DELAY) != pdTRUE) {
			continue;
		}
		if (request == NET_TODO_REQUEST_SYNC) {
			net_service_sync_todos_once();
		}
	}
}

static void net_service_todo_sync_cb(void *arg)
{
	(void)arg;

	app_settings_t settings = { 0 };
	settings_model_get(&settings);
	const uint32_t interval_s = settings.todo_refresh_min > 0U ? (uint32_t)settings.todo_refresh_min * 60U :
								 APP_TODO_SYNC_INTERVAL_S;
	const int64_t now_us = esp_timer_get_time();
	if (s_last_todo_auto_sync_us > 0 && (now_us - s_last_todo_auto_sync_us) < (int64_t)interval_s * 1000000LL) {
		return;
	}

	if (net_service_queue_todo_sync() == 0) {
		s_last_todo_auto_sync_us = now_us;
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
		s_last_todo_auto_sync_us = 0;
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

	s_todo_request_queue = xQueueCreate(1, sizeof(net_todo_request_t));
	if (s_todo_request_queue == NULL) {
		ESP_LOGE(TAG, "failed to create todo request queue");
		err = ESP_ERR_NO_MEM;
		goto fail;
	}

	BaseType_t task_ok = xTaskCreate(net_service_todo_task, "todo_sync_task", 4096, NULL, 6, &s_todo_task);
	if (task_ok != pdPASS) {
		ESP_LOGE(TAG, "failed to create todo sync task");
		err = ESP_ERR_NO_MEM;
		goto fail;
	}

	wifi_config_t wifi_config = { 0 };
	if (strlen(APP_WIFI_STA_SSID) > 0U) {
		strlcpy((char *)wifi_config.sta.ssid, APP_WIFI_STA_SSID, sizeof(wifi_config.sta.ssid));
		strlcpy((char *)wifi_config.sta.password, APP_WIFI_STA_PASSWORD, sizeof(wifi_config.sta.password));
		wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
		wifi_config.sta.pmf_cfg.capable = true;
		wifi_config.sta.pmf_cfg.required = false;
		err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(err));
			goto fail;
		}
	}

	s_status = (app_net_status_t){ 0 };
	net_service_reset_todo_snapshot();
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
	s_todo_sync_in_progress = false;
	s_last_todo_auto_sync_us = 0;
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

int net_service_request_todo_sync_now(void)
{
	return net_service_queue_todo_sync();
}
