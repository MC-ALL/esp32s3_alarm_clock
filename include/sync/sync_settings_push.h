#ifndef SYNC_SETTINGS_PUSH_H_
#define SYNC_SETTINGS_PUSH_H_

#include <config/settings_model.h>
#include <sync/sync_service.h>

typedef void (*sync_settings_push_conflict_fn_t)(void *ctx);

int sync_settings_push_alarms_once(app_device_config_snapshot_t *device_config_snapshot,
				   const app_settings_t *settings,
				   sync_settings_push_conflict_fn_t on_conflict,
				   void *ctx);
int sync_settings_push_voice_once(app_device_config_snapshot_t *device_config_snapshot,
				  const app_settings_t *settings,
				  sync_settings_push_conflict_fn_t on_conflict,
				  void *ctx);

#endif
