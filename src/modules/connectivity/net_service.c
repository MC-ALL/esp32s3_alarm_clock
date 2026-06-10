#include "app_module.h"
#include <app_config.h>
#include <module_common.h>
#include <net_http.h>
#include <net_service.h>
#include <net_time.h>
#include <net_wifi_platform.h>
#include <net_wifi_runtime.h>

#include <esp_event.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <lwip/inet.h>
#include <string.h>

static const char *TAG = "net";

static net_wifi_runtime_t s_wifi_runtime;

static void net_service_reconnect_cb(void *arg)
{
	(void)arg;
	s_wifi_runtime.reconnect_timer_running = false;
	const app_net_status_t *status = net_wifi_runtime_const_status(&s_wifi_runtime);
	if (!net_wifi_platform_is_initialized() || (status != NULL && status->wifi_connected) ||
	    strlen(APP_WIFI_STA_SSID) == 0U) {
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

	int err = net_wifi_platform_init(net_event_handler, NULL);
	if (err != 0) {
		goto fail;
	}

	err = net_wifi_runtime_create_timers(&s_wifi_runtime, net_service_reconnect_cb,
					     net_service_sntp_sync_cb);
	if (err != 0) {
		goto fail;
	}

	err = net_wifi_platform_configure_sta(APP_WIFI_STA_SSID, APP_WIFI_STA_PASSWORD);
	if (err != 0) {
		goto fail;
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

	net_wifi_platform_deinit();
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
	if (!net_wifi_platform_is_initialized() || status == NULL || !status->wifi_started) {
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
