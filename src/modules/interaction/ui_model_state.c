#include <ui_model_state.h>

#include <stddef.h>

void ui_model_state_make_renderer(const ui_model_state_refs_t *refs, ui_renderer_state_t *out_state)
{
	if (refs == NULL || out_state == NULL || refs->settings == NULL) {
		return;
	}

	const ui_settings_store_state_t *settings = refs->settings;
	*out_state = (ui_renderer_state_t){
		.main_page = refs->main_page != NULL ? *refs->main_page : UI_PAGE_HOME,
		.view = refs->view != NULL ? *refs->view : UI_VIEW_MAIN,
		.focus = refs->focus != NULL ? *refs->focus : 0,
		.alarm_selected = refs->alarm_selected != NULL ? *refs->alarm_selected : 0,
		.todo_selected = refs->todo_selected != NULL ? *refs->todo_selected : 0,
		.home_clock_only = settings->home_clock_only,
		.use_24h = settings->use_24h,
		.low_use_24h = settings->low_use_24h,
		.home_hour_chime_on = settings->home_hour_chime_on,
		.alarm_voice_on = settings->alarm_voice_on,
		.todo_voice_on = settings->todo_voice_on,
		.env_voice_on = settings->env_voice_on,
		.env_alert_on = settings->env_alert_on,
		.env_sample_s = settings->env_sample_s,
		.env_temp_low_c = settings->env_temp_low_c,
		.env_temp_high_c = settings->env_temp_high_c,
		.env_humi_low_percent = settings->env_humi_low_percent,
		.env_humi_high_percent = settings->env_humi_high_percent,
		.env_lux_low = settings->env_lux_low,
		.env_lux_high = settings->env_lux_high,
		.low_enter_absent_s = settings->low_enter_absent_s,
		.low_exit_present_s = settings->low_exit_present_s,
		.alarms = settings->alarms,
		.alarm_count = settings->alarm_count,
		.alarm_page_focus = refs->alarm_page_focus,
		.todo_page_focus = refs->todo_page_focus,
	};
}

void ui_model_state_make_settings_controller(const ui_model_state_refs_t *refs,
					     ui_settings_controller_state_t *out_state)
{
	if (refs == NULL || out_state == NULL || refs->settings == NULL) {
		return;
	}

	ui_settings_store_state_t *settings = refs->settings;
	*out_state = (ui_settings_controller_state_t){
		.view = refs->view,
		.focus = refs->focus,
		.alarm_selected = refs->alarm_selected,
		.todo_selected = refs->todo_selected,
		.home_clock_only = &settings->home_clock_only,
		.use_24h = &settings->use_24h,
		.low_use_24h = &settings->low_use_24h,
		.home_hour_chime_on = &settings->home_hour_chime_on,
		.alarm_voice_on = &settings->alarm_voice_on,
		.todo_voice_on = &settings->todo_voice_on,
		.env_voice_on = &settings->env_voice_on,
		.env_alert_on = &settings->env_alert_on,
		.env_sample_s = &settings->env_sample_s,
		.env_temp_low_c = &settings->env_temp_low_c,
		.env_temp_high_c = &settings->env_temp_high_c,
		.env_humi_low_percent = &settings->env_humi_low_percent,
		.env_humi_high_percent = &settings->env_humi_high_percent,
		.env_lux_low = &settings->env_lux_low,
		.env_lux_high = &settings->env_lux_high,
		.low_enter_absent_s = &settings->low_enter_absent_s,
		.low_exit_present_s = &settings->low_exit_present_s,
		.alarms = settings->alarms,
		.alarm_count = &settings->alarm_count,
		.save_settings = refs->save_settings,
		.save_alarm_settings = refs->save_alarm_settings,
		.save_voice_settings = refs->save_voice_settings,
	};
}

void ui_model_state_make_navigation(const ui_model_state_refs_t *refs, ui_navigation_state_t *out_state)
{
	if (refs == NULL || out_state == NULL || refs->settings == NULL || refs->low_clock_runtime == NULL) {
		return;
	}

	*out_state = (ui_navigation_state_t){
		.main_page = refs->main_page,
		.view = refs->view,
		.focus = refs->focus,
		.alarm_page_focus = refs->alarm_page_focus,
		.todo_page_focus = refs->todo_page_focus,
		.todo_selected = refs->todo_selected,
		.alarm_count = refs->settings->alarm_count,
		.low_clock_auto_entered = &refs->low_clock_runtime->auto_entered,
		.process_settings_ok = refs->process_settings_ok,
		.log_info = refs->log_info,
	};
}
