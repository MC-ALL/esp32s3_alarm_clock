#include <settings_defaults.h>

void settings_defaults_apply(app_settings_t *settings)
{
	if (settings == NULL) {
		return;
	}

	*settings = (app_settings_t){
		.use_24h = true,
		.low_use_24h = true,
		.home_hour_chime_on = true,
		.alarm_voice_on = true,
		.todo_voice_on = true,
		.env_voice_on = true,
		.env_alert_on = true,
		.todo_refresh_min = 5,
		.env_sample_s = 10,
		.env_temp_low_c = 10,
		.env_temp_high_c = 35,
		.env_humi_low_percent = 30,
		.env_humi_high_percent = 80,
		.env_lux_low = 20,
		.env_lux_high = 1000,
		.alarm_count = 3,
		.low_enter_absent_s = 60,
		.low_exit_present_s = 3,
		.alarms = {
			{ .hour = 7, .minute = 30, .repeat = true, .enabled = true, .voice = true },
			{ .hour = 8, .minute = 0, .repeat = true, .enabled = true, .voice = true },
			{ .hour = 20, .minute = 15, .repeat = false, .enabled = true, .voice = false },
		},
	};
}

void settings_defaults_sanitize(app_settings_t *settings)
{
	if (settings == NULL) {
		return;
	}

	if (settings->alarm_count > APP_SETTINGS_MAX_ALARMS) {
		settings->alarm_count = APP_SETTINGS_MAX_ALARMS;
	}
	if (settings->todo_refresh_min == 0U) {
		settings->todo_refresh_min = 5U;
	} else if (settings->todo_refresh_min > 30U) {
		settings->todo_refresh_min = 30U;
	}
	if (settings->env_sample_s < 5U) {
		settings->env_sample_s = 5U;
	} else if (settings->env_sample_s > 30U) {
		settings->env_sample_s = 30U;
	}
	if (settings->env_temp_low_c >= settings->env_temp_high_c) {
		settings->env_temp_low_c = 10;
		settings->env_temp_high_c = 35;
	}
	if (settings->env_humi_low_percent >= settings->env_humi_high_percent ||
	    settings->env_humi_high_percent > 100U) {
		settings->env_humi_low_percent = 30U;
		settings->env_humi_high_percent = 80U;
	}
	if (settings->env_lux_low >= settings->env_lux_high) {
		settings->env_lux_low = 20U;
		settings->env_lux_high = 1000U;
	}
	if (settings->low_enter_absent_s < 5U) {
		settings->low_enter_absent_s = 5U;
	}
	if (settings->low_exit_present_s == 0U) {
		settings->low_exit_present_s = 1U;
	}

	for (uint8_t i = 0; i < settings->alarm_count; i++) {
		settings->alarms[i].hour %= 24U;
		settings->alarms[i].minute %= 60U;
	}
}
