#ifndef APP_NET_SERVICE_H_
#define APP_NET_SERVICE_H_

#include <stdbool.h>
#include <stdint.h>

#include <app/settings_model.h>

typedef struct {
	bool wifi_started;
	bool wifi_connected;
	bool ip_ready;
	bool time_synced;
	char connected_ssid[33];
	char ip_addr[16];
	int8_t rssi;
} app_net_status_t;

typedef struct {
	char id[24];
	char text[96];
	bool done;
	char updated_at[40];
} app_todo_item_t;

#define APP_TODO_MAX_ITEMS 8

typedef struct {
	bool sync_ok;
	bool sync_in_progress;
	uint8_t count;
	char last_error[64];
	char last_sync_at[32];
	app_todo_item_t items[APP_TODO_MAX_ITEMS];
} app_todo_snapshot_t;

typedef struct {
	uint32_t config_version;
	char updated_at[40];
	app_settings_t settings;
} app_device_config_snapshot_t;

bool net_service_get_status(app_net_status_t *out_status);
int net_service_request_connect_now(void);
bool net_service_get_todo_snapshot(app_todo_snapshot_t *out_snapshot);
bool net_service_get_device_config_snapshot(app_device_config_snapshot_t *out_snapshot);
int net_service_request_todo_sync_now(void);
int net_service_request_todo_set_done(const char *todo_id, bool done);
int net_service_request_todo_delete(const char *todo_id);
int net_service_request_push_alarm_settings(const app_settings_t *settings);
int net_service_request_push_voice_settings(const app_settings_t *settings);
int net_service_request_report_event(const char *event_type, const char *todo_id);

#endif
