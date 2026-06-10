#include <sync/sync_settings_push.h>

#include <core/app_bus.h>
#include <sync/sync_protocol.h>
#include <sync/sync_transport.h>

#include <stdlib.h>
#include <string.h>

static void sync_settings_push_publish_error(const char *reason)
{
	app_bus_event_t event = {
		.type = APP_BUS_EVENT_SYNC_FAILED,
	};
	strlcpy(event.data.error.reason, reason != NULL ? reason : "sync failed", sizeof(event.data.error.reason));
	(void)app_bus_publish(&event);
}

static int sync_settings_push_once(app_device_config_snapshot_t *device_config_snapshot,
				   const app_settings_t *settings,
				   const char *path,
				   char *(*build_body)(uint32_t config_version, const app_settings_t *settings),
				   const char *oom_reason,
				   const char *fail_reason,
				   sync_settings_push_conflict_fn_t on_conflict,
				   void *ctx)
{
	if (device_config_snapshot == NULL || settings == NULL || !sync_transport_net_ready()) {
		return -1;
	}

	char *body = build_body(device_config_snapshot->config_version, settings);
	if (body == NULL) {
		sync_settings_push_publish_error(oom_reason);
		return -1;
	}

	char response[256] = { 0 };
	int status = 0;
	int ret = sync_transport_http_request("PUT", path, body, response, sizeof(response), &status);
	free(body);
	if (ret == 0 && status == 200) {
		device_config_snapshot->settings = *settings;
		sync_protocol_parse_config_version_response(response, &device_config_snapshot->config_version,
							    device_config_snapshot->updated_at,
							    sizeof(device_config_snapshot->updated_at));
		return 0;
	}
	if (status == 409) {
		if (on_conflict != NULL) {
			on_conflict(ctx);
		}
		return 0;
	}
	sync_settings_push_publish_error(fail_reason);
	return -1;
}

int sync_settings_push_alarms_once(app_device_config_snapshot_t *device_config_snapshot,
				   const app_settings_t *settings,
				   sync_settings_push_conflict_fn_t on_conflict,
				   void *ctx)
{
	return sync_settings_push_once(device_config_snapshot, settings, "/api/device/alarms",
				       sync_protocol_build_alarm_settings, "alarm push oom", "alarm push failed",
				       on_conflict, ctx);
}

int sync_settings_push_voice_once(app_device_config_snapshot_t *device_config_snapshot,
				  const app_settings_t *settings,
				  sync_settings_push_conflict_fn_t on_conflict,
				  void *ctx)
{
	return sync_settings_push_once(device_config_snapshot, settings, "/api/device/voice-settings",
				       sync_protocol_build_voice_settings, "voice push oom", "voice push failed",
				       on_conflict, ctx);
}
