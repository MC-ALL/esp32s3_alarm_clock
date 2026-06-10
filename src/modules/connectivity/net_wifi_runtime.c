#include <net_wifi_runtime.h>

#include <esp_err.h>
#include <string.h>

#define NET_WIFI_RECONNECT_DELAY_INITIAL_S 5U
#define NET_WIFI_RECONNECT_DELAY_MAX_S 60U

void net_wifi_runtime_init(net_wifi_runtime_t *runtime)
{
	if (runtime == NULL) {
		return;
	}

	runtime->status = (app_net_status_t){ 0 };
	runtime->reconnect_delay_s = NET_WIFI_RECONNECT_DELAY_INITIAL_S;
}

int net_wifi_runtime_create_timers(net_wifi_runtime_t *runtime, esp_timer_cb_t reconnect_cb,
				   esp_timer_cb_t sntp_sync_cb)
{
	if (runtime == NULL) {
		return (int)ESP_ERR_INVALID_ARG;
	}

	const esp_timer_create_args_t reconnect_timer_args = {
		.callback = reconnect_cb,
		.name = "wifi_retry",
	};
	esp_err_t err = esp_timer_create(&reconnect_timer_args, &runtime->reconnect_timer);
	if (err != ESP_OK) {
		return (int)err;
	}

	const esp_timer_create_args_t sntp_timer_args = {
		.callback = sntp_sync_cb,
		.name = "sntp_sync",
	};
	err = esp_timer_create(&sntp_timer_args, &runtime->sntp_sync_timer);
	if (err != ESP_OK) {
		(void)esp_timer_delete(runtime->reconnect_timer);
		runtime->reconnect_timer = NULL;
		return (int)err;
	}

	return 0;
}

void net_wifi_runtime_delete_timers(net_wifi_runtime_t *runtime)
{
	if (runtime == NULL) {
		return;
	}

	net_wifi_runtime_stop_reconnect_timer(runtime);
	net_wifi_runtime_stop_sntp_sync_timer(runtime);
	if (runtime->reconnect_timer != NULL) {
		(void)esp_timer_delete(runtime->reconnect_timer);
		runtime->reconnect_timer = NULL;
	}
	if (runtime->sntp_sync_timer != NULL) {
		(void)esp_timer_delete(runtime->sntp_sync_timer);
		runtime->sntp_sync_timer = NULL;
	}
}

void net_wifi_runtime_stop_reconnect_timer(net_wifi_runtime_t *runtime)
{
	if (runtime != NULL && runtime->reconnect_timer != NULL && runtime->reconnect_timer_running) {
		(void)esp_timer_stop(runtime->reconnect_timer);
		runtime->reconnect_timer_running = false;
	}
}

void net_wifi_runtime_start_reconnect_timer(net_wifi_runtime_t *runtime, bool ssid_configured)
{
	if (runtime == NULL || runtime->reconnect_timer == NULL || runtime->reconnect_timer_running ||
	    !ssid_configured) {
		return;
	}

	uint64_t delay_us = (uint64_t)runtime->reconnect_delay_s * 1000000ULL;
	if (esp_timer_start_once(runtime->reconnect_timer, delay_us) == ESP_OK) {
		runtime->reconnect_timer_running = true;
	}
}

void net_wifi_runtime_stop_sntp_sync_timer(net_wifi_runtime_t *runtime)
{
	if (runtime != NULL && runtime->sntp_sync_timer != NULL && runtime->sntp_sync_timer_running) {
		(void)esp_timer_stop(runtime->sntp_sync_timer);
		runtime->sntp_sync_timer_running = false;
	}
}

void net_wifi_runtime_start_sntp_sync_timer(net_wifi_runtime_t *runtime)
{
	if (runtime == NULL || runtime->sntp_sync_timer == NULL || runtime->sntp_sync_timer_running) {
		return;
	}
	if (esp_timer_start_periodic(runtime->sntp_sync_timer, 120000000ULL) == ESP_OK) {
		runtime->sntp_sync_timer_running = true;
	}
}

void net_wifi_runtime_mark_started(net_wifi_runtime_t *runtime)
{
	if (runtime != NULL) {
		runtime->status.wifi_started = true;
	}
}

void net_wifi_runtime_mark_disconnected(net_wifi_runtime_t *runtime)
{
	if (runtime == NULL) {
		return;
	}

	runtime->status.wifi_connected = false;
	runtime->status.ip_ready = false;
	runtime->status.time_synced = false;
	runtime->status.connected_ssid[0] = '\0';
	runtime->status.ip_addr[0] = '\0';
	runtime->status.rssi = 0;
}

void net_wifi_runtime_mark_got_ip(net_wifi_runtime_t *runtime, const char *ssid, const char *ip_addr)
{
	if (runtime == NULL) {
		return;
	}

	runtime->status.wifi_connected = true;
	runtime->status.ip_ready = true;
	strlcpy(runtime->status.connected_ssid, ssid != NULL ? ssid : "", sizeof(runtime->status.connected_ssid));
	strlcpy(runtime->status.ip_addr, ip_addr != NULL ? ip_addr : "", sizeof(runtime->status.ip_addr));
}

void net_wifi_runtime_backoff_reconnect(net_wifi_runtime_t *runtime)
{
	if (runtime == NULL || runtime->reconnect_delay_s >= NET_WIFI_RECONNECT_DELAY_MAX_S) {
		return;
	}

	runtime->reconnect_delay_s *= 2U;
	if (runtime->reconnect_delay_s > NET_WIFI_RECONNECT_DELAY_MAX_S) {
		runtime->reconnect_delay_s = NET_WIFI_RECONNECT_DELAY_MAX_S;
	}
}

void net_wifi_runtime_reset_reconnect_delay(net_wifi_runtime_t *runtime)
{
	if (runtime != NULL) {
		runtime->reconnect_delay_s = NET_WIFI_RECONNECT_DELAY_INITIAL_S;
	}
}

uint32_t net_wifi_runtime_reconnect_delay_s(const net_wifi_runtime_t *runtime)
{
	return runtime != NULL ? runtime->reconnect_delay_s : NET_WIFI_RECONNECT_DELAY_INITIAL_S;
}

app_net_status_t *net_wifi_runtime_status(net_wifi_runtime_t *runtime)
{
	return runtime != NULL ? &runtime->status : NULL;
}

const app_net_status_t *net_wifi_runtime_const_status(const net_wifi_runtime_t *runtime)
{
	return runtime != NULL ? &runtime->status : NULL;
}
