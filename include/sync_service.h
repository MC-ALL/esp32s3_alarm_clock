#ifndef APP_SYNC_SERVICE_H_
#define APP_SYNC_SERVICE_H_

#include <stdbool.h>
#include <stdint.h>

#include <settings_model.h>

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

bool sync_service_get_todo_snapshot(app_todo_snapshot_t *out_snapshot);
bool sync_service_get_device_config_snapshot(app_device_config_snapshot_t *out_snapshot);

#endif
