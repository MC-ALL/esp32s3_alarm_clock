#ifndef NET_WIFI_RUNTIME_H_
#define NET_WIFI_RUNTIME_H_

#include <stdbool.h>
#include <stdint.h>

#include <esp_timer.h>
#include <connectivity/net_service.h>

typedef struct {
	app_net_status_t status;
	esp_timer_handle_t reconnect_timer;
	bool reconnect_timer_running;
	esp_timer_handle_t sntp_sync_timer;
	bool sntp_sync_timer_running;
	uint32_t reconnect_delay_s;
} net_wifi_runtime_t;

void net_wifi_runtime_init(net_wifi_runtime_t *runtime);
int net_wifi_runtime_create_timers(net_wifi_runtime_t *runtime, esp_timer_cb_t reconnect_cb,
				   esp_timer_cb_t sntp_sync_cb);
void net_wifi_runtime_delete_timers(net_wifi_runtime_t *runtime);
void net_wifi_runtime_stop_reconnect_timer(net_wifi_runtime_t *runtime);
void net_wifi_runtime_start_reconnect_timer(net_wifi_runtime_t *runtime, bool ssid_configured);
void net_wifi_runtime_stop_sntp_sync_timer(net_wifi_runtime_t *runtime);
void net_wifi_runtime_start_sntp_sync_timer(net_wifi_runtime_t *runtime);
void net_wifi_runtime_mark_started(net_wifi_runtime_t *runtime);
void net_wifi_runtime_mark_disconnected(net_wifi_runtime_t *runtime);
void net_wifi_runtime_mark_got_ip(net_wifi_runtime_t *runtime, const char *ssid, const char *ip_addr);
void net_wifi_runtime_backoff_reconnect(net_wifi_runtime_t *runtime);
void net_wifi_runtime_reset_reconnect_delay(net_wifi_runtime_t *runtime);
uint32_t net_wifi_runtime_reconnect_delay_s(const net_wifi_runtime_t *runtime);
app_net_status_t *net_wifi_runtime_status(net_wifi_runtime_t *runtime);
const app_net_status_t *net_wifi_runtime_const_status(const net_wifi_runtime_t *runtime);

#endif
