#ifndef UI_RENDERER_H_
#define UI_RENDERER_H_

#include <stdbool.h>
#include <stdint.h>

#include <ui_types.h>

typedef struct {
	ui_main_page_t main_page;
	ui_view_t view;
	uint8_t focus;
	uint8_t alarm_selected;
	uint8_t todo_selected;
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
	uint8_t *alarm_page_focus;
	uint8_t *todo_page_focus;
} ui_renderer_state_t;

void ui_renderer_render(const ui_renderer_state_t *state);

#endif
