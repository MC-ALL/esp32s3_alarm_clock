#ifndef UI_NAVIGATION_H_
#define UI_NAVIGATION_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <ui_types.h>

typedef void (*ui_navigation_action_fn_t)(void *ctx);
typedef void (*ui_navigation_log_fn_t)(const char *message, void *ctx);

typedef struct {
	ui_main_page_t *main_page;
	ui_view_t *view;
	uint8_t *focus;
	uint8_t *alarm_page_focus;
	uint8_t *todo_page_focus;
	uint8_t *todo_selected;
	uint8_t alarm_count;
	bool *low_clock_auto_entered;
	ui_navigation_action_fn_t process_settings_ok;
	void *process_settings_ok_ctx;
	ui_navigation_log_fn_t log_info;
	void *log_ctx;
} ui_navigation_state_t;

bool ui_navigation_process_key(const ui_navigation_state_t *state, size_t key_index);

#endif
