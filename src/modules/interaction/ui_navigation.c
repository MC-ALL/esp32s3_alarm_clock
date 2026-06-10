#include <ui_navigation.h>

#include <sync_service.h>
#include <ui_todo_view.h>

static uint8_t focus_count_for_view(const ui_navigation_state_t *state)
{
	switch (*state->view) {
	case UI_VIEW_HOME_SETTINGS:
		return 3;
	case UI_VIEW_ALARM_SETTINGS:
		return (uint8_t)(4U + state->alarm_count);
	case UI_VIEW_ALARM_ITEM:
		return 4;
	case UI_VIEW_TODO_SETTINGS:
		return 4;
	case UI_VIEW_TODO_ITEM:
		return 2;
	case UI_VIEW_TODO_DELETE_CONFIRM:
		return 2;
	case UI_VIEW_ENV_SETTINGS:
		return 10;
	case UI_VIEW_LOW_SETTINGS:
		return 3;
	default:
		return 1;
	}
}

static void enter_settings_for_page(const ui_navigation_state_t *state)
{
	*state->focus = 0;
	switch (*state->main_page) {
	case UI_PAGE_HOME:
		*state->view = UI_VIEW_HOME_SETTINGS;
		break;
	case UI_PAGE_ALARM:
		*state->view = UI_VIEW_ALARM_SETTINGS;
		break;
	case UI_PAGE_TODO:
		*state->view = UI_VIEW_TODO_SETTINGS;
		break;
	case UI_PAGE_ENV:
		*state->view = UI_VIEW_ENV_SETTINGS;
		break;
	case UI_PAGE_WIFI:
		if (state->log_info != NULL) {
			state->log_info("wifi page has no settings", state->log_ctx);
		}
		break;
	case UI_PAGE_LOW_CLOCK:
		*state->view = UI_VIEW_LOW_SETTINGS;
		break;
	default:
		break;
	}
}

static void leave_low_clock_auto_mode(const ui_navigation_state_t *state)
{
	if (state->low_clock_auto_entered != NULL) {
		*state->low_clock_auto_entered = false;
	}
}

static void process_main_key(const ui_navigation_state_t *state, size_t key_index)
{
	if (key_index == UI_KEY_HOME_BACK) {
		if (*state->main_page == UI_PAGE_ALARM) {
			if (state->alarm_count > 0U) {
				*state->alarm_page_focus = (uint8_t)((*state->alarm_page_focus + 1U) % state->alarm_count);
			}
		} else if (*state->main_page == UI_PAGE_TODO) {
			app_todo_snapshot_t todo = { 0 };
			(void)sync_service_get_todo_snapshot(&todo);
			const uint8_t count = (uint8_t)(ui_todo_item_count(&todo) + 1U);
			if (count > 0U) {
				*state->todo_page_focus = (uint8_t)((*state->todo_page_focus + 1U) % count);
			}
		} else {
			*state->main_page = UI_PAGE_HOME;
			leave_low_clock_auto_mode(state);
		}
	} else if (key_index == UI_KEY_PREV_UP) {
		*state->main_page = (ui_main_page_t)((*state->main_page + UI_MAIN_PAGE_COUNT - 1U) % UI_MAIN_PAGE_COUNT);
		leave_low_clock_auto_mode(state);
	} else if (key_index == UI_KEY_NEXT_DOWN) {
		*state->main_page = (ui_main_page_t)((*state->main_page + 1U) % UI_MAIN_PAGE_COUNT);
		leave_low_clock_auto_mode(state);
	} else if (*state->main_page == UI_PAGE_TODO) {
		app_todo_snapshot_t todo = { 0 };
		(void)sync_service_get_todo_snapshot(&todo);
		const uint8_t item_count = ui_todo_item_count(&todo);
		if (*state->todo_page_focus < item_count) {
			*state->todo_selected = *state->todo_page_focus;
			*state->view = UI_VIEW_TODO_ITEM;
			*state->focus = 0;
		} else {
			*state->view = UI_VIEW_TODO_SETTINGS;
			*state->focus = 0;
		}
	} else {
		enter_settings_for_page(state);
	}
}

static void process_back_key(const ui_navigation_state_t *state)
{
	if (*state->view == UI_VIEW_ALARM_ITEM) {
		*state->view = UI_VIEW_ALARM_SETTINGS;
	} else if (*state->view == UI_VIEW_TODO_DELETE_CONFIRM) {
		*state->view = UI_VIEW_TODO_ITEM;
	} else {
		*state->view = UI_VIEW_MAIN;
	}
	*state->focus = 0;
}

bool ui_navigation_process_key(const ui_navigation_state_t *state, size_t key_index)
{
	if (state == NULL || state->main_page == NULL || state->view == NULL || state->focus == NULL ||
	    key_index >= UI_KEY_COUNT) {
		return false;
	}

	if (*state->view == UI_VIEW_MAIN) {
		process_main_key(state, key_index);
		return true;
	}

	if (key_index == UI_KEY_HOME_BACK) {
		process_back_key(state);
		return true;
	}

	const uint8_t count = focus_count_for_view(state);
	if (key_index == UI_KEY_PREV_UP) {
		*state->focus = (uint8_t)((*state->focus + count - 1U) % count);
	} else if (key_index == UI_KEY_NEXT_DOWN) {
		*state->focus = (uint8_t)((*state->focus + 1U) % count);
	} else if (state->process_settings_ok != NULL) {
		state->process_settings_ok(state->process_settings_ok_ctx);
	}
	return true;
}
