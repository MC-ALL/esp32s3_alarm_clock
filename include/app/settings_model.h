#ifndef APP_SETTINGS_MODEL_H_
#define APP_SETTINGS_MODEL_H_

#include <stdbool.h>
#include <stdint.h>

#define APP_SETTINGS_MAX_ALARMS 5U

typedef struct {
	uint8_t hour;
	uint8_t minute;
	bool repeat;
	bool enabled;
	bool voice;
} app_alarm_setting_t;

typedef struct {
	bool home_clock_only;
	bool use_24h;
	bool low_use_24h;
	bool home_hour_chime_on;
	bool alarm_voice_on;
	bool todo_voice_on;
	bool env_voice_on;
	bool env_alert_on;
	uint8_t todo_refresh_min;
	uint8_t env_sample_s;
	int16_t env_temp_low_c;
	int16_t env_temp_high_c;
	uint16_t env_humi_low_percent;
	uint16_t env_humi_high_percent;
	uint16_t env_lux_low;
	uint16_t env_lux_high;
	uint8_t alarm_count;
	uint16_t low_enter_absent_s;
	uint16_t low_exit_present_s;
	app_alarm_setting_t alarms[APP_SETTINGS_MAX_ALARMS];
} app_settings_t;

void settings_model_defaults(app_settings_t *settings);
bool settings_model_load(app_settings_t *settings);
int settings_model_save(const app_settings_t *settings);
void settings_model_get(app_settings_t *settings);
int settings_model_set(const app_settings_t *settings);

#endif
