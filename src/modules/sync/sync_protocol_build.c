#include <sync/sync_protocol.h>

#include <third_party/cJSON.h>

static cJSON *create_telemetry_base(const char *timestamp_key, const char *timestamp,
				    const app_environment_snapshot_t *env,
				    const app_presence_status_t *presence)
{
	cJSON *root = cJSON_CreateObject();
	if (root == NULL) {
		return NULL;
	}
	(void)cJSON_AddStringToObject(root, "device_id", "clock-001");
	(void)cJSON_AddStringToObject(root, timestamp_key, timestamp != NULL ? timestamp : "");
	(void)cJSON_AddNumberToObject(root, "temperature_c", env != NULL ? env->temperature_c : 0.0f);
	(void)cJSON_AddNumberToObject(root, "humidity_percent", env != NULL ? env->humidity_percent : 0.0f);
	(void)cJSON_AddNumberToObject(root, "lux", env != NULL ? env->lux : 0.0f);
	(void)cJSON_AddBoolToObject(root, "presence_detected", presence != NULL && presence->detected);
	return root;
}

char *sync_protocol_build_status_report(const char *sent_at, const app_environment_snapshot_t *env,
					const app_presence_status_t *presence)
{
	cJSON *root = create_telemetry_base("sent_at", sent_at, env, presence);
	if (root == NULL) {
		return NULL;
	}
	(void)cJSON_AddBoolToObject(root, "online", true);
	char *out = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	return out;
}

char *sync_protocol_build_event_report(const char *event_at, const char *event_type, const char *todo_id,
				       const app_environment_snapshot_t *env,
				       const app_presence_status_t *presence)
{
	cJSON *root = create_telemetry_base("event_at", event_at, env, presence);
	if (root == NULL) {
		return NULL;
	}
	(void)cJSON_AddStringToObject(root, "event_type", event_type != NULL ? event_type : "");
	if (todo_id != NULL && todo_id[0] != '\0') {
		(void)cJSON_AddStringToObject(root, "todo_id", todo_id);
	}
	char *out = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	return out;
}

char *sync_protocol_build_alarm_settings(uint32_t config_version, const app_settings_t *settings)
{
	if (settings == NULL) {
		return NULL;
	}
	cJSON *root = cJSON_CreateObject();
	cJSON *alarms = cJSON_CreateArray();
	if (root == NULL || alarms == NULL) {
		cJSON_Delete(root);
		cJSON_Delete(alarms);
		return NULL;
	}
	(void)cJSON_AddNumberToObject(root, "config_version", (double)config_version);
	(void)cJSON_AddItemToObject(root, "alarms", alarms);

	for (uint8_t i = 0; i < settings->alarm_count && i < APP_SETTINGS_MAX_ALARMS; i++) {
		const app_alarm_setting_t *alarm = &settings->alarms[i];
		cJSON *entry = cJSON_CreateObject();
		if (entry == NULL) {
			cJSON_Delete(root);
			return NULL;
		}
		(void)cJSON_AddNumberToObject(entry, "hour", (double)alarm->hour);
		(void)cJSON_AddNumberToObject(entry, "minute", (double)alarm->minute);
		(void)cJSON_AddBoolToObject(entry, "repeat", alarm->repeat);
		(void)cJSON_AddBoolToObject(entry, "enabled", alarm->enabled);
		(void)cJSON_AddBoolToObject(entry, "voice", alarm->voice);
		(void)cJSON_AddItemToArray(alarms, entry);
	}

	char *out = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	return out;
}

char *sync_protocol_build_voice_settings(uint32_t config_version, const app_settings_t *settings)
{
	if (settings == NULL) {
		return NULL;
	}
	cJSON *root = cJSON_CreateObject();
	cJSON *voice = cJSON_CreateObject();
	if (root == NULL || voice == NULL) {
		cJSON_Delete(root);
		cJSON_Delete(voice);
		return NULL;
	}
	(void)cJSON_AddNumberToObject(root, "config_version", (double)config_version);
	(void)cJSON_AddItemToObject(root, "voice_settings", voice);
	(void)cJSON_AddBoolToObject(voice, "todo_voice_on", settings->todo_voice_on);
	(void)cJSON_AddBoolToObject(voice, "alarm_voice_on", settings->alarm_voice_on);
	(void)cJSON_AddBoolToObject(voice, "env_voice_on", settings->env_voice_on);
	(void)cJSON_AddBoolToObject(voice, "env_alert_on", settings->env_alert_on);
	(void)cJSON_AddBoolToObject(voice, "home_hour_chime_on", settings->home_hour_chime_on);

	char *out = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	return out;
}
