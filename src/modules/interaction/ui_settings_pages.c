#include <ui_settings_pages.h>

#include <sync_service.h>
#include <ui_format.h>
#include <ui_layout.h>
#include <ui_style.h>
#include <ui_todo_view.h>

#include <stdio.h>

static void render_row(lv_obj_t *parent, uint8_t row, const char *text, bool selected)
{
	const int32_t y = 4 + row * 44;
	ui_box(parent, 4, y, 228, 38, selected ? ui_color_yellow() : ui_color_panel());
	ui_label(parent, text, UI_FONT_24, selected ? ui_color_black() : ui_color_white(), 8, y + 5, 220, 30,
		 LV_TEXT_ALIGN_LEFT);
}

static lv_obj_t *render_settings_area(lv_obj_t *screen, const char *title, lv_color_t bg)
{
	ui_box(screen, 0, 0, UI_SCREEN_W, UI_CONTENT_H, bg);
	ui_label(screen, title, UI_FONT_32, ui_color_white(), 4, 4, 232, 40, LV_TEXT_ALIGN_CENTER);
	return ui_scroll_area(screen, 0, 48, UI_SCREEN_W, UI_CONTENT_H - 48, bg);
}

static void scroll_focus_into_view(lv_obj_t *area, uint8_t focus)
{
	const int32_t row_y = 4 + focus * 44;
	int32_t scroll_y = row_y - 52;
	if (scroll_y < 0) {
		scroll_y = 0;
	}
	lv_obj_scroll_to_y(area, scroll_y, LV_ANIM_OFF);
}

void ui_settings_pages_render_home(lv_obj_t *screen, const ui_settings_pages_state_t *state)
{
	if (state == NULL) {
		return;
	}
	lv_obj_t *area = render_settings_area(screen, "HOME SET", ui_color_blue());
	render_row(area, 0, state->home_clock_only ? "LAYOUT CLOCK" : "LAYOUT FULL", state->focus == 0U);
	render_row(area, 1, state->use_24h ? "FORMAT 24H" : "FORMAT 12H", state->focus == 1U);
	render_row(area, 2, state->home_hour_chime_on ? "HOUR TONE ON" : "HOUR TONE OFF", state->focus == 2U);
	scroll_focus_into_view(area, state->focus);
}

void ui_settings_pages_render_alarm(lv_obj_t *screen, const ui_settings_pages_state_t *state)
{
	if (state == NULL) {
		return;
	}
	lv_obj_t *area = render_settings_area(screen, "ALARM SET", ui_color_orange());
	render_row(area, 0, state->alarm_voice_on ? "VOICE ON" : "VOICE OFF", state->focus == 0U);
	render_row(area, 1, "TEST VOICE", state->focus == 1U);
	render_row(area, 2, "ADD ALARM", state->focus == 2U);
	render_row(area, 3, "DELETE LAST", state->focus == 3U);

	for (uint8_t i = 0; i < state->alarm_count; i++) {
		char text[32];
		char time_text[16];
		ui_format_alarm_time(state->alarms[i].hour, state->alarms[i].minute, time_text, sizeof(time_text));
		snprintf(text, sizeof(text), "%u  %s %s", (unsigned)(i + 1), time_text,
			 state->alarms[i].enabled ? "ON" : "OFF");
		render_row(area, (uint8_t)(4U + i), text, state->focus == (uint8_t)(4U + i));
	}
	scroll_focus_into_view(area, state->focus);
}

void ui_settings_pages_render_alarm_item(lv_obj_t *screen, const ui_settings_pages_state_t *state)
{
	if (state == NULL || state->alarms == NULL || state->alarm_selected >= state->alarm_count) {
		return;
	}
	lv_obj_t *area = render_settings_area(screen, "ALARM ITEM", ui_color_orange());
	const ui_alarm_item_t *alarm = &state->alarms[state->alarm_selected];
	char time_text[24];
	ui_format_alarm_time(alarm->hour, alarm->minute, time_text, sizeof(time_text));
	char row[32];
	snprintf(row, sizeof(row), "TIME %s", time_text);
	render_row(area, 0, row, state->focus == 0U);
	render_row(area, 1, alarm->repeat ? "REPEAT ON" : "REPEAT OFF", state->focus == 1U);
	render_row(area, 2, alarm->voice ? "VOICE ON" : "VOICE OFF", state->focus == 2U);
	render_row(area, 3, alarm->enabled ? "ENABLE ON" : "ENABLE OFF", state->focus == 3U);
	scroll_focus_into_view(area, state->focus);
}

void ui_settings_pages_render_todo(lv_obj_t *screen, const ui_settings_pages_state_t *state)
{
	if (state == NULL) {
		return;
	}
	lv_obj_t *area = render_settings_area(screen, "TODO SET", ui_color_purple());
	render_row(area, 0, "SYNC NOW", state->focus == 0U);
	render_row(area, 1, "PULL 30S", state->focus == 1U);
	render_row(area, 2, state->todo_voice_on ? "VOICE ON" : "VOICE OFF", state->focus == 2U);
	render_row(area, 3, "TEST VOICE", state->focus == 3U);
	scroll_focus_into_view(area, state->focus);
}

void ui_settings_pages_render_todo_item(lv_obj_t *screen, const ui_settings_pages_state_t *state)
{
	if (state == NULL) {
		return;
	}
	app_todo_snapshot_t todo = { 0 };
	(void)sync_service_get_todo_snapshot(&todo);
	const app_todo_item_t *item = ui_todo_item_at(&todo, state->todo_selected);
	lv_obj_t *area = render_settings_area(screen, "TODO ITEM", ui_color_purple());

	if (item == NULL) {
		render_row(area, 0, "ITEM MISSING", true);
		return;
	}

	char title[64];
	snprintf(title, sizeof(title), "%.40s", item->text[0] != '\0' ? item->text : "(empty)");
	ui_label_wrap(area, title, UI_FONT_20, ui_color_white(), 8, 4, 224, 38, LV_TEXT_ALIGN_LEFT);
	render_row(area, 1, item->done ? "UNDO DONE" : "MARK DONE", state->focus == 0U);
	render_row(area, 2, "DELETE", state->focus == 1U);
	scroll_focus_into_view(area, state->focus);
}

void ui_settings_pages_render_todo_delete_confirm(lv_obj_t *screen, const ui_settings_pages_state_t *state)
{
	if (state == NULL) {
		return;
	}
	app_todo_snapshot_t todo = { 0 };
	(void)sync_service_get_todo_snapshot(&todo);
	const app_todo_item_t *item = ui_todo_item_at(&todo, state->todo_selected);
	lv_obj_t *area = render_settings_area(screen, "DELETE?", ui_color_red());
	char title[64];

	snprintf(title, sizeof(title), "%.40s", item != NULL && item->text[0] != '\0' ? item->text : "TODO ITEM");
	ui_label_wrap(area, title, UI_FONT_20, ui_color_white(), 8, 4, 224, 38, LV_TEXT_ALIGN_LEFT);
	render_row(area, 1, "CONFIRM DELETE", state->focus == 0U);
	render_row(area, 2, "CANCEL", state->focus == 1U);
	scroll_focus_into_view(area, state->focus);
}

void ui_settings_pages_render_env(lv_obj_t *screen, const ui_settings_pages_state_t *state)
{
	if (state == NULL) {
		return;
	}
	lv_obj_t *area = render_settings_area(screen, "ENV SET", ui_color_green());
	render_row(area, 0, state->env_voice_on ? "VOICE ON" : "VOICE OFF", state->focus == 0U);
	render_row(area, 1, "TEST VOICE", state->focus == 1U);
	char sample[32];
	snprintf(sample, sizeof(sample), "SAMPLE %uS", (unsigned)state->env_sample_s);
	render_row(area, 2, sample, state->focus == 2U);
	char row[32];
	snprintf(row, sizeof(row), "T LOW %dC", (int)state->env_temp_low_c);
	render_row(area, 3, row, state->focus == 3U);
	snprintf(row, sizeof(row), "T HIGH %dC", (int)state->env_temp_high_c);
	render_row(area, 4, row, state->focus == 4U);
	snprintf(row, sizeof(row), "H LOW %u%%", (unsigned)state->env_humi_low_percent);
	render_row(area, 5, row, state->focus == 5U);
	snprintf(row, sizeof(row), "H HIGH %u%%", (unsigned)state->env_humi_high_percent);
	render_row(area, 6, row, state->focus == 6U);
	snprintf(row, sizeof(row), "L LOW %u", (unsigned)state->env_lux_low);
	render_row(area, 7, row, state->focus == 7U);
	snprintf(row, sizeof(row), "L HIGH %u", (unsigned)state->env_lux_high);
	render_row(area, 8, row, state->focus == 8U);
	render_row(area, 9, state->env_alert_on ? "ALERT ON" : "ALERT OFF", state->focus == 9U);
	scroll_focus_into_view(area, state->focus);
}

void ui_settings_pages_render_low(lv_obj_t *screen, const ui_settings_pages_state_t *state)
{
	if (state == NULL) {
		return;
	}
	lv_obj_t *area = render_settings_area(screen, "CLOCK SET", ui_color_dark());
	char enter_text[32];
	char exit_text[32];
	snprintf(enter_text, sizeof(enter_text), "ENTER %uS", (unsigned)state->low_enter_absent_s);
	snprintf(exit_text, sizeof(exit_text), "EXIT %uS", (unsigned)state->low_exit_present_s);
	render_row(area, 0, state->low_use_24h ? "FORMAT 24H" : "FORMAT 12H", state->focus == 0U);
	render_row(area, 1, enter_text, state->focus == 1U);
	render_row(area, 2, exit_text, state->focus == 2U);
	scroll_focus_into_view(area, state->focus);
}
