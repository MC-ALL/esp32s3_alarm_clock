#include <interaction/ui_settings_store.h>

#include <core/app_bus.h>
#include <config/settings_model.h>
#include <interaction/ui_actions.h>

#include <esp_log.h>

static const char *TAG = "ui_settings";

static void ui_settings_store_apply(ui_settings_store_state_t *state, const app_settings_t *settings)
{
	if (state == NULL || settings == NULL) {
		return;
	}

	state->home_clock_only = settings->home_clock_only;
	state->use_24h = settings->use_24h;
	state->low_use_24h = settings->low_use_24h;
	state->home_hour_chime_on = settings->home_hour_chime_on;
	state->alarm_voice_on = settings->alarm_voice_on;
	state->todo_voice_on = settings->todo_voice_on;
	state->env_voice_on = settings->env_voice_on;
	state->env_alert_on = settings->env_alert_on;
	state->todo_refresh_min = settings->todo_refresh_min;
	state->env_sample_s = settings->env_sample_s;
	state->env_temp_low_c = settings->env_temp_low_c;
	state->env_temp_high_c = settings->env_temp_high_c;
	state->env_humi_low_percent = settings->env_humi_low_percent;
	state->env_humi_high_percent = settings->env_humi_high_percent;
	state->env_lux_low = settings->env_lux_low;
	state->env_lux_high = settings->env_lux_high;
	state->low_enter_absent_s = settings->low_enter_absent_s;
	state->low_exit_present_s = settings->low_exit_present_s;
	state->alarm_count = settings->alarm_count > UI_MAX_ALARMS ? UI_MAX_ALARMS : settings->alarm_count;

	for (uint8_t i = 0; i < state->alarm_count; i++) {
		state->alarms[i] = (ui_alarm_item_t){
			.hour = settings->alarms[i].hour,
			.minute = settings->alarms[i].minute,
			.repeat = settings->alarms[i].repeat,
			.enabled = settings->alarms[i].enabled,
			.voice = settings->alarms[i].voice,
		};
	}
}

static void ui_settings_store_collect(const ui_settings_store_state_t *state, app_settings_t *settings)
{
	if (state == NULL || settings == NULL) {
		return;
	}

	settings_model_defaults(settings);
	settings->home_clock_only = state->home_clock_only;
	settings->use_24h = state->use_24h;
	settings->low_use_24h = state->low_use_24h;
	settings->home_hour_chime_on = state->home_hour_chime_on;
	settings->alarm_voice_on = state->alarm_voice_on;
	settings->todo_voice_on = state->todo_voice_on;
	settings->env_voice_on = state->env_voice_on;
	settings->env_alert_on = state->env_alert_on;
	settings->todo_refresh_min = state->todo_refresh_min;
	settings->env_sample_s = state->env_sample_s;
	settings->env_temp_low_c = state->env_temp_low_c;
	settings->env_temp_high_c = state->env_temp_high_c;
	settings->env_humi_low_percent = state->env_humi_low_percent;
	settings->env_humi_high_percent = state->env_humi_high_percent;
	settings->env_lux_low = state->env_lux_low;
	settings->env_lux_high = state->env_lux_high;
	settings->low_enter_absent_s = state->low_enter_absent_s;
	settings->low_exit_present_s = state->low_exit_present_s;
	settings->alarm_count = state->alarm_count > APP_SETTINGS_MAX_ALARMS ? APP_SETTINGS_MAX_ALARMS : state->alarm_count;

	for (uint8_t i = 0; i < settings->alarm_count; i++) {
		settings->alarms[i] = (app_alarm_setting_t){
			.hour = state->alarms[i].hour,
			.minute = state->alarms[i].minute,
			.repeat = state->alarms[i].repeat,
			.enabled = state->alarms[i].enabled,
			.voice = state->alarms[i].voice,
		};
	}
}

void ui_settings_store_set_defaults(ui_settings_store_state_t *state)
{
	if (state == NULL) {
		return;
	}

	app_settings_t settings = { 0 };
	settings_model_defaults(&settings);
	ui_settings_store_apply(state, &settings);
}

void ui_settings_store_load(ui_settings_store_state_t *state)
{
	if (state == NULL) {
		return;
	}

	app_settings_t settings = { 0 };
	settings_model_get(&settings);
	ui_settings_store_apply(state, &settings);
}

bool ui_settings_store_save(const ui_settings_store_state_t *state, bool publish_alarm, bool publish_voice)
{
	if (state == NULL) {
		return false;
	}

	app_settings_t settings = { 0 };
	ui_settings_store_collect(state, &settings);

	int ret = settings_model_set(&settings);
	if (ret != 0) {
		ESP_LOGW(TAG, "settings save failed ret=%d", ret);
		return false;
	}

	ui_actions_publish_settings(APP_BUS_EVENT_SETTINGS_CHANGED, &settings);
	if (publish_alarm) {
		ui_actions_publish_settings(APP_BUS_EVENT_ALARM_SETTINGS_CHANGED, &settings);
	}
	if (publish_voice) {
		ui_actions_publish_settings(APP_BUS_EVENT_VOICE_SETTINGS_CHANGED, &settings);
	}
	return true;
}
