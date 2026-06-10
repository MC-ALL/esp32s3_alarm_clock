#ifndef APP_SYNC_PROTOCOL_H_
#define APP_SYNC_PROTOCOL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <environment_service.h>
#include <presence_service.h>
#include <settings_model.h>
#include <sync_service.h>

bool sync_protocol_parse_device_config(const char *json, const app_settings_t *base_settings,
				       app_device_config_snapshot_t *snapshot,
				       app_todo_snapshot_t *todo_snapshot);
void sync_protocol_parse_config_version_response(const char *json, uint32_t *config_version,
						 char *updated_at, size_t updated_at_size);

char *sync_protocol_build_status_report(const char *sent_at, const app_environment_snapshot_t *env,
					const app_presence_status_t *presence);
char *sync_protocol_build_event_report(const char *event_at, const char *event_type, const char *todo_id,
				       const app_environment_snapshot_t *env,
				       const app_presence_status_t *presence);
char *sync_protocol_build_alarm_settings(uint32_t config_version, const app_settings_t *settings);
char *sync_protocol_build_voice_settings(uint32_t config_version, const app_settings_t *settings);

#endif
