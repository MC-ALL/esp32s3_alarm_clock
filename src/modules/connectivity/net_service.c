#include "app_module.h"
#include <app_config.h>
#include <module_common.h>
#include <net_http.h>
#include <net_service.h>
#include <net_time.h>
#include <net_wifi_runtime.h>

#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <lwip/inet.h>
#include <string.h>

static const char *TAG = "net";

static bool s_event_loop_owned;
static bool s_wifi_initialized;
static esp_netif_t *s_wifi_sta_netif;
static esp_event_handler_instance_t s_wifi_event_instance;
static esp_event_handler_instance_t s_ip_event_instance;
static bool s_wifi_event_registered;
static bool s_ip_event_registered;
static net_wifi_runtime_t s_wifi_runtime;

static void net_service_reconnect_cb(void *arg)
{
	(void)arg;
	s_wifi_runtime.reconnect_timer_running = false;
	const app_net_status_t *status = net_wifi_runtime_const_status(&s_wifi_runtime);
	if (!s_wifi_initialized || (status != NULL && status->wifi_connected) || strlen(APP_WIFI_STA_SSID) == 0U) {
		return;
	}
	ESP_LOGI(TAG, "wifi retry connect to %s delay=%us", APP_WIFI_STA_SSID,
		 (unsigned)net_wifi_runtime_reconnect_delay_s(&s_wifi_runtime));
	(void)esp_wifi_connect();
	net_wifi_runtime_backoff_reconnect(&s_wifi_runtime);
}

static void net_service_sntp_sync_cb(void *arg)
{
	(void)arg;
	app_net_status_t *status = net_wifi_runtime_status(&s_wifi_runtime);
	if (status == NULL || !status->wifi_connected || !status->ip_ready) {
		return;
	}
	status->time_synced = false;
	net_time_start_sntp(status->time_synced);
	net_time_try_mark_synced(&status->time_synced);
}

static void net_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
	(void)arg;

	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		net_wifi_runtime_mark_started(&s_wifi_runtime);
		if (strlen(APP_WIFI_STA_SSID) > 0U) {
			net_wifi_runtime_stop_reconnect_timer(&s_wifi_runtime);
			(void)esp_wifi_connect();
		}
		return;
	}

	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		const wifi_event_sta_disconnected_t *event = (const wifi_event_sta_disconnected_t *)event_data;
		net_wifi_runtime_mark_disconnected(&s_wifi_runtime);
		ESP_LOGW(TAG, "wifi disconnected reason=%d retry_delay=%us",
			 event != NULL ? (int)event->reason : -1,
			 (unsigned)net_wifi_runtime_reconnect_delay_s(&s_wifi_runtime));
		net_wifi_runtime_start_reconnect_timer(&s_wifi_runtime, strlen(APP_WIFI_STA_SSID) > 0U);
		return;
	}

	if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
		char ip_addr[16] = { 0 };
		if (event != NULL) {
			(void)snprintf(ip_addr, sizeof(ip_addr), IPSTR, IP2STR(&event->ip_info.ip));
		}
		net_wifi_runtime_mark_got_ip(&s_wifi_runtime, APP_WIFI_STA_SSID, ip_addr);
		net_wifi_runtime_reset_reconnect_delay(&s_wifi_runtime);
		net_wifi_runtime_stop_reconnect_timer(&s_wifi_runtime);
		app_net_status_t *status = net_wifi_runtime_status(&s_wifi_runtime);
		if (status != NULL) {
			net_time_start_sntp(status->time_synced);
			net_wifi_runtime_start_sntp_sync_timer(&s_wifi_runtime);
			net_time_try_mark_synced(&status->time_synced);
			ESP_LOGI(TAG, "got ip=%s", status->ip_addr);
		}
		return;
	}
}

int net_service_init(void)
{
	net_time_configure_timezone();

	esp_err_t err = esp_netif_init();
	if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
		return (int)err;
	}

	err = esp_event_loop_create_default();
	if (err == ESP_OK) {
		s_event_loop_owned = true;
	} else if (err != ESP_ERR_INVALID_STATE) {
		return (int)err;
	}

	s_wifi_sta_netif = esp_netif_create_default_wifi_sta();
	if (s_wifi_sta_netif == NULL) {
		err = ESP_FAIL;
		goto fail;
	}

	wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
	wifi_init_cfg.nvs_enable = false;
	err = esp_wifi_init(&wifi_init_cfg);
	if (err != ESP_OK) {
		goto fail;
	}
	s_wifi_initialized = true;

	err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &net_event_handler, NULL,
						  &s_wifi_event_instance);
	if (err != ESP_OK) {
		goto fail;
	}
	s_wifi_event_registered = true;

	err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &net_event_handler, NULL,
						  &s_ip_event_instance);
	if (err != ESP_OK) {
		goto fail;
	}
	s_ip_event_registered = true;

	err = esp_wifi_set_mode(WIFI_MODE_STA);
	if (err != ESP_OK) {
		goto fail;
	}
	err = esp_wifi_set_ps(WIFI_PS_NONE);
	if (err != ESP_OK) {
		goto fail;
	}

	err = (esp_err_t)net_wifi_runtime_create_timers(&s_wifi_runtime, net_service_reconnect_cb,
							net_service_sntp_sync_cb);
	if (err != ESP_OK) {
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
			goto fail;
		}
	}

	net_wifi_runtime_init(&s_wifi_runtime);
	return 0;

fail:
	(void)net_service_stop();
	return (int)err;
}

int net_service_start(void)
{
	esp_err_t err = esp_wifi_start();
	return err == ESP_OK ? 0 : (int)err;
}

int net_service_stop(void)
{
	net_time_stop_sntp();
	net_wifi_runtime_stop_reconnect_timer(&s_wifi_runtime);
	net_wifi_runtime_stop_sntp_sync_timer(&s_wifi_runtime);

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
	net_wifi_runtime_delete_timers(&s_wifi_runtime);
	net_wifi_runtime_init(&s_wifi_runtime);
	return 0;
}

bool net_service_get_status(app_net_status_t *out_status)
{
	if (out_status == NULL) {
		return false;
	}
	app_net_status_t *status = net_wifi_runtime_status(&s_wifi_runtime);
	if (status == NULL) {
		return false;
	}
	net_time_try_mark_synced(&status->time_synced);
	*out_status = *status;
	return true;
}

int net_service_request_connect_now(void)
{
	const app_net_status_t *status = net_wifi_runtime_const_status(&s_wifi_runtime);
	if (!s_wifi_initialized || status == NULL || !status->wifi_started) {
		return -1;
	}
	net_wifi_runtime_stop_reconnect_timer(&s_wifi_runtime);
	net_wifi_runtime_reset_reconnect_delay(&s_wifi_runtime);
	esp_err_t err = esp_wifi_connect();
	if (err != ESP_OK) {
		net_wifi_runtime_start_reconnect_timer(&s_wifi_runtime, strlen(APP_WIFI_STA_SSID) > 0U);
		return (int)err;
	}
	return 0;
}

int net_service_http_request(const char *method, const char *url, const char *body,
			     app_net_http_response_t *response)
{
	return net_http_request(method, url, body, response);
}
