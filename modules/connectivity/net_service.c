#include "app_module.h"
#include <app/app_config.h>
#include <app/net_service.h>
#include <app/module_common.h>

#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <lwip/apps/sntp.h>
#include <string.h>
#include <time.h>

static const char *TAG = "net";
static bool s_event_loop_ready;
static esp_netif_t *s_wifi_sta_netif;
static esp_event_handler_instance_t s_wifi_event_instance;
static esp_event_handler_instance_t s_ip_event_instance;
static app_net_status_t s_status;

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

static void net_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
	(void)arg;
	(void)event_data;

	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		s_status.wifi_started = true;
		if (strlen(APP_WIFI_STA_SSID) > 0U) {
			ESP_LOGI(TAG, "wifi start, connecting to %s", APP_WIFI_STA_SSID);
			(void)esp_wifi_connect();
		} else {
			ESP_LOGI(TAG, "wifi credentials not configured, skip connect");
		}
		return;
	}

	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		s_status.wifi_connected = false;
		s_status.ip_ready = false;
		s_status.time_synced = false;
		if (strlen(APP_WIFI_STA_SSID) > 0U) {
			ESP_LOGW(TAG, "wifi disconnected, reconnect");
			(void)esp_wifi_connect();
		}
		return;
	}

	if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		s_status.wifi_connected = true;
		s_status.ip_ready = true;
		net_service_start_sntp();
		net_service_try_mark_time_synced();
		ESP_LOGI(TAG, "got ip, sntp started");
	}
}

int net_service_init(void)
{
	esp_err_t err = esp_netif_init();
	if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
		ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	err = esp_event_loop_create_default();
	if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
		ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
		return (int)err;
	}
	s_event_loop_ready = true;

	s_wifi_sta_netif = esp_netif_create_default_wifi_sta();
	if (s_wifi_sta_netif == NULL) {
		ESP_LOGE(TAG, "esp_netif_create_default_wifi_sta failed");
		return -1;
	}

	wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
	err = esp_wifi_init(&wifi_init_cfg);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
		&net_event_handler, NULL, &s_wifi_event_instance);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "register WIFI_EVENT failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
		&net_event_handler, NULL, &s_ip_event_instance);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "register IP_EVENT failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	err = esp_wifi_set_mode(WIFI_MODE_STA);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	wifi_config_t wifi_config = { 0 };
	if (strlen(APP_WIFI_STA_SSID) > 0U) {
		strlcpy((char *)wifi_config.sta.ssid, APP_WIFI_STA_SSID, sizeof(wifi_config.sta.ssid));
		strlcpy((char *)wifi_config.sta.password, APP_WIFI_STA_PASSWORD, sizeof(wifi_config.sta.password));
		wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
		wifi_config.sta.pmf_cfg.capable = true;
		wifi_config.sta.pmf_cfg.required = false;
		err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(err));
			return (int)err;
		}
	}

	s_status = (app_net_status_t){ 0 };
	ESP_LOGI(TAG, "init");
	return 0;
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
	s_status.time_synced = false;

	(void)esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_event_instance);
	(void)esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_event_instance);
	(void)esp_wifi_stop();
	(void)esp_wifi_deinit();

	if (s_event_loop_ready) {
		(void)esp_event_loop_delete_default();
		s_event_loop_ready = false;
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
