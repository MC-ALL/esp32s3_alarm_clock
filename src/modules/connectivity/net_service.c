#include "app_module.h"
#include <app_config.h>
#include <module_common.h>
#include <net_http.h>
#include <net_service.h>
#include <net_time.h>

#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_timer.h>
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
static app_net_status_t s_status;
static esp_timer_handle_t s_reconnect_timer;
static bool s_reconnect_timer_running;
static esp_timer_handle_t s_sntp_sync_timer;
static bool s_sntp_sync_timer_running;
static uint32_t s_reconnect_delay_s = 5;

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
	uint64_t delay_us = (uint64_t)s_reconnect_delay_s * 1000000ULL;
	if (esp_timer_start_once(s_reconnect_timer, delay_us) == ESP_OK) {
		s_reconnect_timer_running = true;
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

static void net_service_reconnect_cb(void *arg)
{
	(void)arg;
	s_reconnect_timer_running = false;
	if (!s_wifi_initialized || s_status.wifi_connected || strlen(APP_WIFI_STA_SSID) == 0U) {
		return;
	}
	ESP_LOGI(TAG, "wifi retry connect to %s delay=%us", APP_WIFI_STA_SSID, (unsigned)s_reconnect_delay_s);
	(void)esp_wifi_connect();
	if (s_reconnect_delay_s < 60U) {
		s_reconnect_delay_s *= 2U;
		if (s_reconnect_delay_s > 60U) {
			s_reconnect_delay_s = 60U;
		}
	}
}

static void net_service_sntp_sync_cb(void *arg)
{
	(void)arg;
	if (!s_status.wifi_connected || !s_status.ip_ready) {
		return;
	}
	s_status.time_synced = false;
	net_time_start_sntp(s_status.time_synced);
	net_time_try_mark_synced(&s_status.time_synced);
}

static void net_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
	(void)arg;

	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		s_status.wifi_started = true;
		if (strlen(APP_WIFI_STA_SSID) > 0U) {
			net_service_stop_reconnect_timer();
			(void)esp_wifi_connect();
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
		ESP_LOGW(TAG, "wifi disconnected reason=%d retry_delay=%us",
			 event != NULL ? (int)event->reason : -1, (unsigned)s_reconnect_delay_s);
		net_service_start_reconnect_timer();
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
		s_reconnect_delay_s = 5;
		net_service_stop_reconnect_timer();
		net_time_start_sntp(s_status.time_synced);
		net_service_start_sntp_sync_timer();
		net_time_try_mark_synced(&s_status.time_synced);
		ESP_LOGI(TAG, "got ip=%s", s_status.ip_addr);
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

	const esp_timer_create_args_t reconnect_timer_args = {
		.callback = net_service_reconnect_cb,
		.name = "wifi_retry",
	};
	err = esp_timer_create(&reconnect_timer_args, &s_reconnect_timer);
	if (err != ESP_OK) {
		goto fail;
	}

	const esp_timer_create_args_t sntp_timer_args = {
		.callback = net_service_sntp_sync_cb,
		.name = "sntp_sync",
	};
	err = esp_timer_create(&sntp_timer_args, &s_sntp_sync_timer);
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

	s_status = (app_net_status_t){ 0 };
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
	s_status = (app_net_status_t){ 0 };
	net_service_stop_reconnect_timer();
	net_service_stop_sntp_sync_timer();
	s_reconnect_delay_s = 5;

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
	return 0;
}

bool net_service_get_status(app_net_status_t *out_status)
{
	if (out_status == NULL) {
		return false;
	}
	net_time_try_mark_synced(&s_status.time_synced);
	*out_status = s_status;
	return true;
}

int net_service_request_connect_now(void)
{
	if (!s_wifi_initialized || !s_status.wifi_started) {
		return -1;
	}
	net_service_stop_reconnect_timer();
	s_reconnect_delay_s = 5;
	esp_err_t err = esp_wifi_connect();
	if (err != ESP_OK) {
		net_service_start_reconnect_timer();
		return (int)err;
	}
	return 0;
}

int net_service_http_request(const char *method, const char *url, const char *body,
			     app_net_http_response_t *response)
{
	return net_http_request(method, url, body, response);
}
