#ifndef UI_MODEL_STATE_H_
#define UI_MODEL_STATE_H_

#include <interaction/ui_low_clock_runtime.h>
#include <interaction/ui_navigation.h>
#include <interaction/ui_renderer.h>
#include <interaction/ui_settings_controller.h>
#include <interaction/ui_settings_store.h>
#include <interaction/ui_types.h>

typedef struct {
	ui_main_page_t *main_page;
	ui_view_t *view;
	uint8_t *focus;
	uint8_t *alarm_selected;
	uint8_t *alarm_page_focus;
	uint8_t *todo_page_focus;
	uint8_t *todo_selected;
	ui_low_clock_runtime_t *low_clock_runtime;
	ui_settings_store_state_t *settings;
	ui_settings_controller_save_fn_t save_settings;
	ui_settings_controller_save_fn_t save_alarm_settings;
	ui_settings_controller_save_fn_t save_voice_settings;
	ui_navigation_action_fn_t process_settings_ok;
	ui_navigation_log_fn_t log_info;
} ui_model_state_refs_t;

void ui_model_state_make_renderer(const ui_model_state_refs_t *refs, ui_renderer_state_t *out_state);
void ui_model_state_make_settings_controller(const ui_model_state_refs_t *refs,
					     ui_settings_controller_state_t *out_state);
void ui_model_state_make_navigation(const ui_model_state_refs_t *refs, ui_navigation_state_t *out_state);

#endif
