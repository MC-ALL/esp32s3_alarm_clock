#include <interaction/ui_renderer.h>

#include <interaction/display_service.h>
#include <sync/sync_service.h>
#include <interaction/ui_layout.h>
#include <interaction/ui_pages.h>
#include <interaction/ui_settings_pages.h>
#include <interaction/ui_style.h>
#include <interaction/ui_status_pages.h>
#include <interaction/ui_todo_view.h>

#include <lvgl.h>
#include <stddef.h>

static const char *ok_icon_for_state(const ui_renderer_state_t *state)
{
	if (state->view == UI_VIEW_MAIN) {
		if (state->main_page == UI_PAGE_TODO) {
			app_todo_snapshot_t todo = { 0 };
			(void)sync_service_get_todo_snapshot(&todo);
			return *state->todo_page_focus < ui_todo_item_count(&todo) ? LV_SYMBOL_OK : LV_SYMBOL_SETTINGS;
		}
		return state->main_page == UI_PAGE_WIFI ? LV_SYMBOL_CLOSE : LV_SYMBOL_SETTINGS;
	}
	return LV_SYMBOL_OK;
}

static void render_keybar(lv_obj_t *screen, const ui_renderer_state_t *state)
{
	const bool main_list_page = state->view == UI_VIEW_MAIN &&
				    (state->main_page == UI_PAGE_ALARM || state->main_page == UI_PAGE_TODO);
	const char *icons[UI_KEY_COUNT] = {
		state->view == UI_VIEW_MAIN ? (main_list_page ? LV_SYMBOL_DOWN : LV_SYMBOL_HOME) : LV_SYMBOL_LEFT,
		state->view == UI_VIEW_MAIN ? LV_SYMBOL_LEFT : LV_SYMBOL_UP,
		state->view == UI_VIEW_MAIN ? LV_SYMBOL_RIGHT : LV_SYMBOL_DOWN,
		ok_icon_for_state(state),
	};

	for (size_t i = 0; i < UI_KEY_COUNT; i++) {
		ui_box(screen, (int32_t)(i * UI_KEY_W), UI_KEYBAR_Y, UI_KEY_W, UI_KEYBAR_H, ui_color_black());
		ui_label(screen, icons[i], UI_FONT_32, ui_color_white(), (int32_t)(i * UI_KEY_W), UI_KEYBAR_Y + 22,
			 UI_KEY_W, 40, LV_TEXT_ALIGN_CENTER);
	}
}

static void render_main_page(lv_obj_t *screen, const ui_renderer_state_t *state)
{
	ui_pages_state_t pages = {
		.home_clock_only = state->home_clock_only,
		.use_24h = state->use_24h,
		.low_use_24h = state->low_use_24h,
		.env_alert_on = state->env_alert_on,
		.env_temp_low_c = state->env_temp_low_c,
		.env_temp_high_c = state->env_temp_high_c,
		.env_humi_low_percent = state->env_humi_low_percent,
		.env_humi_high_percent = state->env_humi_high_percent,
		.env_lux_low = state->env_lux_low,
		.env_lux_high = state->env_lux_high,
		.alarms = state->alarms,
		.alarm_count = state->alarm_count,
		.alarm_page_focus = state->alarm_page_focus,
		.todo_page_focus = state->todo_page_focus,
	};

	switch (state->main_page) {
	case UI_PAGE_HOME:
		ui_pages_render_home(screen, &pages);
		break;
	case UI_PAGE_ALARM:
		ui_pages_render_alarm(screen, &pages);
		break;
	case UI_PAGE_TODO:
		ui_pages_render_todo(screen, &pages);
		break;
	case UI_PAGE_ENV:
		ui_status_pages_render_env(screen, &pages);
		break;
	case UI_PAGE_WIFI:
		ui_status_pages_render_wifi(screen);
		break;
	case UI_PAGE_LOW_CLOCK:
		ui_pages_render_low_clock(screen, &pages);
		break;
	default:
		break;
	}
}

static void render_settings_page(lv_obj_t *screen, const ui_renderer_state_t *state)
{
	ui_settings_pages_state_t settings_pages = {
		.focus = state->focus,
		.home_clock_only = state->home_clock_only,
		.use_24h = state->use_24h,
		.low_use_24h = state->low_use_24h,
		.home_hour_chime_on = state->home_hour_chime_on,
		.alarm_voice_on = state->alarm_voice_on,
		.todo_voice_on = state->todo_voice_on,
		.env_voice_on = state->env_voice_on,
		.env_alert_on = state->env_alert_on,
		.env_sample_s = state->env_sample_s,
		.env_temp_low_c = state->env_temp_low_c,
		.env_temp_high_c = state->env_temp_high_c,
		.env_humi_low_percent = state->env_humi_low_percent,
		.env_humi_high_percent = state->env_humi_high_percent,
		.env_lux_low = state->env_lux_low,
		.env_lux_high = state->env_lux_high,
		.low_enter_absent_s = state->low_enter_absent_s,
		.low_exit_present_s = state->low_exit_present_s,
		.alarms = state->alarms,
		.alarm_count = state->alarm_count,
		.alarm_selected = state->alarm_selected,
		.todo_selected = state->todo_selected,
	};

	switch (state->view) {
	case UI_VIEW_HOME_SETTINGS:
		ui_settings_pages_render_home(screen, &settings_pages);
		break;
	case UI_VIEW_ALARM_SETTINGS:
		ui_settings_pages_render_alarm(screen, &settings_pages);
		break;
	case UI_VIEW_ALARM_ITEM:
		ui_settings_pages_render_alarm_item(screen, &settings_pages);
		break;
	case UI_VIEW_TODO_SETTINGS:
		ui_settings_pages_render_todo(screen, &settings_pages);
		break;
	case UI_VIEW_TODO_ITEM:
		ui_settings_pages_render_todo_item(screen, &settings_pages);
		break;
	case UI_VIEW_TODO_DELETE_CONFIRM:
		ui_settings_pages_render_todo_delete_confirm(screen, &settings_pages);
		break;
	case UI_VIEW_ENV_SETTINGS:
		ui_settings_pages_render_env(screen, &settings_pages);
		break;
	case UI_VIEW_LOW_SETTINGS:
		ui_settings_pages_render_low(screen, &settings_pages);
		break;
	default:
		break;
	}
}

void ui_renderer_render(const ui_renderer_state_t *state)
{
	if (state == NULL) {
		return;
	}

	lv_obj_t *screen = display_service_get_screen();
	if (screen == NULL) {
		return;
	}

	lv_obj_clean(screen);
	ui_obj_plain(screen, ui_color_black());
	lv_obj_set_size(screen, UI_SCREEN_W, UI_SCREEN_H);

	if (state->view == UI_VIEW_MAIN) {
		render_main_page(screen, state);
	} else {
		render_settings_page(screen, state);
	}
	render_keybar(screen, state);
}
