#ifndef UI_SETTINGS_STORE_H_
#define UI_SETTINGS_STORE_H_

#include <stdbool.h>
#include <stdint.h>

#include <ui_types.h>

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
	uint16_t low_enter_absent_s;
	uint16_t low_exit_present_s;
	ui_alarm_item_t alarms[UI_MAX_ALARMS];
	uint8_t alarm_count;
} ui_settings_store_state_t;

void ui_settings_store_set_defaults(ui_settings_store_state_t *state);
void ui_settings_store_load(ui_settings_store_state_t *state);
bool ui_settings_store_save(const ui_settings_store_state_t *state, bool publish_alarm, bool publish_voice);

#endif
