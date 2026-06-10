#include <sync_protocol.h>

#include <cJSON.h>
#include <string.h>

static void json_copy_string(const cJSON *object, const char *key, char *out, size_t out_size)
{
	if (object == NULL || key == NULL || out == NULL || out_size == 0U) {
		return;
	}
	const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
	if (!cJSON_IsString(item)) {
		return;
	}
	strlcpy(out, item->valuestring, out_size);
}

static bool json_get_bool(const cJSON *object, const char *key, bool fallback)
{
	const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
	if (cJSON_IsBool(item)) {
		return cJSON_IsTrue(item);
	}
	return fallback;
}

static uint32_t json_get_u32(const cJSON *object, const char *key, uint32_t fallback)
{
	const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
	if (cJSON_IsNumber(item) && item->valuedouble >= 0.0) {
		return (uint32_t)item->valuedouble;
	}
	return fallback;
}

static void parse_voice_settings(const cJSON *root, app_settings_t *settings)
{
	const cJSON *voice = cJSON_GetObjectItemCaseSensitive(root, "voice_settings");
	if (!cJSON_IsObject(voice) || settings == NULL) {
		return;
	}
	settings->todo_voice_on = json_get_bool(voice, "todo_voice_on", settings->todo_voice_on);
	settings->alarm_voice_on = json_get_bool(voice, "alarm_voice_on", settings->alarm_voice_on);
	settings->env_voice_on = json_get_bool(voice, "env_voice_on", settings->env_voice_on);
	settings->env_alert_on = json_get_bool(voice, "env_alert_on", settings->env_alert_on);
	settings->home_hour_chime_on = json_get_bool(voice, "home_hour_chime_on", settings->home_hour_chime_on);
}

static void parse_alarm_items(const cJSON *root, app_settings_t *settings)
{
	const cJSON *alarms = cJSON_GetObjectItemCaseSensitive(root, "alarms");
	if (!cJSON_IsArray(alarms) || settings == NULL) {
		return;
	}

	settings->alarm_count = 0;
	const int count = cJSON_GetArraySize(alarms);
	for (int i = 0; i < count && settings->alarm_count < APP_SETTINGS_MAX_ALARMS; i++) {
		const cJSON *entry = cJSON_GetArrayItem(alarms, i);
		if (!cJSON_IsObject(entry)) {
			continue;
		}
		const uint32_t hour = json_get_u32(entry, "hour", UINT32_MAX);
		const uint32_t minute = json_get_u32(entry, "minute", UINT32_MAX);
		if (hour == UINT32_MAX || minute == UINT32_MAX) {
			continue;
		}

		app_alarm_setting_t *alarm = &settings->alarms[settings->alarm_count++];
		*alarm = (app_alarm_setting_t){
			.hour = (uint8_t)(hour % 24U),
			.minute = (uint8_t)(minute % 60U),
			.repeat = json_get_bool(entry, "repeat", false),
			.enabled = json_get_bool(entry, "enabled", false),
			.voice = json_get_bool(entry, "voice", false),
		};
	}
}

static bool parse_todo_items(const cJSON *root, app_todo_snapshot_t *snapshot)
{
	const cJSON *todos = cJSON_GetObjectItemCaseSensitive(root, "todos_active");
	if (!cJSON_IsArray(todos)) {
		todos = cJSON_GetObjectItemCaseSensitive(root, "items");
	}
	if (!cJSON_IsArray(todos) || snapshot == NULL) {
		return false;
	}

	snapshot->count = 0;
	const int count = cJSON_GetArraySize(todos);
	for (int i = 0; i < count && snapshot->count < APP_TODO_MAX_ITEMS; i++) {
		const cJSON *entry = cJSON_GetArrayItem(todos, i);
		if (!cJSON_IsObject(entry)) {
			continue;
		}
		const cJSON *id = cJSON_GetObjectItemCaseSensitive(entry, "id");
		const cJSON *text = cJSON_GetObjectItemCaseSensitive(entry, "text");
		if (!cJSON_IsString(id) || !cJSON_IsString(text)) {
			continue;
		}

		app_todo_item_t *item = &snapshot->items[snapshot->count++];
		strlcpy(item->id, id->valuestring, sizeof(item->id));
		strlcpy(item->text, text->valuestring, sizeof(item->text));
		item->done = json_get_bool(entry, "done", false);
		json_copy_string(entry, "updated_at", item->updated_at, sizeof(item->updated_at));
	}
	return true;
}

bool sync_protocol_parse_device_config(const char *json, const app_settings_t *base_settings,
				       app_device_config_snapshot_t *snapshot,
				       app_todo_snapshot_t *todo_snapshot)
{
	if (json == NULL || base_settings == NULL || snapshot == NULL || todo_snapshot == NULL) {
		return false;
	}

	cJSON *root = cJSON_Parse(json);
	if (!cJSON_IsObject(root)) {
		cJSON_Delete(root);
		return false;
	}

	snapshot->config_version = json_get_u32(root, "config_version", snapshot->config_version);
	json_copy_string(root, "updated_at", snapshot->updated_at, sizeof(snapshot->updated_at));

	app_settings_t settings = *base_settings;
	parse_voice_settings(root, &settings);
	parse_alarm_items(root, &settings);
	const bool ok = parse_todo_items(root, todo_snapshot);
	if (ok) {
		snapshot->settings = settings;
	}
	cJSON_Delete(root);
	return ok;
}

void sync_protocol_parse_config_version_response(const char *json, uint32_t *config_version,
						 char *updated_at, size_t updated_at_size)
{
	if (json == NULL || json[0] == '\0') {
		return;
	}
	cJSON *root = cJSON_Parse(json);
	if (!cJSON_IsObject(root)) {
		cJSON_Delete(root);
		return;
	}
	if (config_version != NULL) {
		*config_version = json_get_u32(root, "config_version", *config_version);
	}
	json_copy_string(root, "updated_at", updated_at, updated_at_size);
	cJSON_Delete(root);
}
