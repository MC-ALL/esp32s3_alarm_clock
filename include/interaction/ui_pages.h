#ifndef APP_UI_PAGES_H_
#define APP_UI_PAGES_H_

#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>
#include <interaction/ui_types.h>

typedef struct {
	bool home_clock_only;
	bool use_24h;
	bool low_use_24h;
	bool env_alert_on;
	int16_t env_temp_low_c;
	int16_t env_temp_high_c;
	uint16_t env_humi_low_percent;
	uint16_t env_humi_high_percent;
	uint16_t env_lux_low;
	uint16_t env_lux_high;
	const ui_alarm_item_t *alarms;
	uint8_t alarm_count;
	uint8_t *alarm_page_focus;
	uint8_t *todo_page_focus;
} ui_pages_state_t;

void ui_pages_render_home(lv_obj_t *screen, const ui_pages_state_t *state);
void ui_pages_render_alarm(lv_obj_t *screen, const ui_pages_state_t *state);
void ui_pages_render_todo(lv_obj_t *screen, const ui_pages_state_t *state);
void ui_pages_render_low_clock(lv_obj_t *screen, const ui_pages_state_t *state);

#endif
