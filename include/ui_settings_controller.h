#ifndef UI_SETTINGS_CONTROLLER_H_
#define UI_SETTINGS_CONTROLLER_H_

#include <stdbool.h>
#include <stdint.h>

#include <ui_types.h>

typedef void (*ui_settings_controller_save_fn_t)(void);

typedef struct {
	ui_view_t *view;
	uint8_t *focus;
	uint8_t *alarm_selected;
	uint8_t *todo_selected;
	bool *home_clock_only;
	bool *use_24h;
	bool *low_use_24h;
	bool *home_hour_chime_on;
	bool *alarm_voice_on;
	bool *todo_voice_on;
	bool *env_voice_on;
	bool *env_alert_on;
	uint8_t *env_sample_s;
	int16_t *env_temp_low_c;
	int16_t *env_temp_high_c;
	uint16_t *env_humi_low_percent;
	uint16_t *env_humi_high_percent;
	uint16_t *env_lux_low;
	uint16_t *env_lux_high;
	uint16_t *low_enter_absent_s;
	uint16_t *low_exit_present_s;
	ui_alarm_item_t *alarms;
	uint8_t *alarm_count;
	ui_settings_controller_save_fn_t save_settings;
	ui_settings_controller_save_fn_t save_alarm_settings;
	ui_settings_controller_save_fn_t save_voice_settings;
} ui_settings_controller_state_t;

void ui_settings_controller_process_ok(const ui_settings_controller_state_t *state);

#endif
