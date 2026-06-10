#ifndef APP_UI_SETTINGS_PAGES_H_
#define APP_UI_SETTINGS_PAGES_H_

#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>
#include <interaction/ui_types.h>

typedef struct {
	uint8_t focus;
	bool home_clock_only;
	bool use_24h;
	bool low_use_24h;
	bool home_hour_chime_on;
	bool alarm_voice_on;
	bool todo_voice_on;
	bool env_voice_on;
	bool env_alert_on;
	uint8_t env_sample_s;
	int16_t env_temp_low_c;
	int16_t env_temp_high_c;
	uint16_t env_humi_low_percent;
	uint16_t env_humi_high_percent;
	uint16_t env_lux_low;
	uint16_t env_lux_high;
	uint16_t low_enter_absent_s;
	uint16_t low_exit_present_s;
	const ui_alarm_item_t *alarms;
	uint8_t alarm_count;
	uint8_t alarm_selected;
	uint8_t todo_selected;
} ui_settings_pages_state_t;

void ui_settings_pages_render_home(lv_obj_t *screen, const ui_settings_pages_state_t *state);
void ui_settings_pages_render_alarm(lv_obj_t *screen, const ui_settings_pages_state_t *state);
void ui_settings_pages_render_alarm_item(lv_obj_t *screen, const ui_settings_pages_state_t *state);
void ui_settings_pages_render_todo(lv_obj_t *screen, const ui_settings_pages_state_t *state);
void ui_settings_pages_render_todo_item(lv_obj_t *screen, const ui_settings_pages_state_t *state);
void ui_settings_pages_render_todo_delete_confirm(lv_obj_t *screen, const ui_settings_pages_state_t *state);
void ui_settings_pages_render_env(lv_obj_t *screen, const ui_settings_pages_state_t *state);
void ui_settings_pages_render_low(lv_obj_t *screen, const ui_settings_pages_state_t *state);

#endif
