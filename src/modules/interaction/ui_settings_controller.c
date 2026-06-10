#include <ui_settings_controller.h>

#include <sync_service.h>
#include <ui_actions.h>
#include <ui_settings_adjust.h>
#include <ui_todo_view.h>

#include <esp_log.h>

static const char *TAG = "ui_settings_controller";

static void add_alarm(const ui_settings_controller_state_t *state)
{
	if (*state->alarm_count >= UI_MAX_ALARMS) {
		ESP_LOGW(TAG, "alarm list full");
		return;
	}

	state->alarms[*state->alarm_count] = (ui_alarm_item_t){
		.hour = 7,
		.minute = 30,
		.repeat = true,
		.enabled = true,
		.voice = true,
	};
	*state->alarm_selected = *state->alarm_count;
	(*state->alarm_count)++;
	*state->view = UI_VIEW_ALARM_ITEM;
	*state->focus = 0;
	state->save_alarm_settings();
	ESP_LOGI(TAG, "alarm added count=%u", (unsigned)*state->alarm_count);
}

static void delete_last_alarm(const ui_settings_controller_state_t *state)
{
	if (*state->alarm_count == 0U) {
		return;
	}

	(*state->alarm_count)--;
	if (*state->alarm_selected >= *state->alarm_count) {
		*state->alarm_selected = *state->alarm_count == 0U ? 0U : (uint8_t)(*state->alarm_count - 1U);
	}
	state->save_alarm_settings();
	ESP_LOGI(TAG, "alarm deleted count=%u", (unsigned)*state->alarm_count);
}

static void process_home_settings(const ui_settings_controller_state_t *state)
{
	if (*state->focus == 0U) {
		*state->home_clock_only = !*state->home_clock_only;
	} else if (*state->focus == 1U) {
		*state->use_24h = !*state->use_24h;
	} else {
		*state->home_hour_chime_on = !*state->home_hour_chime_on;
	}
	state->save_settings();
}

static void process_alarm_settings(const ui_settings_controller_state_t *state)
{
	if (*state->focus == 0U) {
		*state->alarm_voice_on = !*state->alarm_voice_on;
		state->save_voice_settings();
	} else if (*state->focus == 1U) {
		ui_actions_play_alarm_voice_test();
	} else if (*state->focus == 2U) {
		add_alarm(state);
	} else if (*state->focus == 3U) {
		delete_last_alarm(state);
	} else {
		*state->alarm_selected = (uint8_t)(*state->focus - 4U);
		if (*state->alarm_selected < *state->alarm_count) {
			*state->view = UI_VIEW_ALARM_ITEM;
			*state->focus = 0;
		}
	}
}

static void process_alarm_item(const ui_settings_controller_state_t *state)
{
	ui_alarm_item_t *alarm = &state->alarms[*state->alarm_selected];
	if (*state->focus == 0U) {
		alarm->minute = (uint8_t)((alarm->minute + 5U) % 60U);
		if (alarm->minute == 0U) {
			alarm->hour = (uint8_t)((alarm->hour + 1U) % 24U);
		}
	} else if (*state->focus == 1U) {
		alarm->repeat = !alarm->repeat;
	} else if (*state->focus == 2U) {
		alarm->voice = !alarm->voice;
	} else {
		alarm->enabled = !alarm->enabled;
	}
	state->save_alarm_settings();
}

static void process_todo_settings(const ui_settings_controller_state_t *state)
{
	if (*state->focus == 0U) {
		(void)ui_actions_request_todo_sync();
	} else if (*state->focus == 1U) {
		return;
	} else if (*state->focus == 2U) {
		*state->todo_voice_on = !*state->todo_voice_on;
		state->save_voice_settings();
	} else if (*state->focus == 3U) {
		ui_actions_play_todo_voice_test();
	}
}

static void process_todo_item(const ui_settings_controller_state_t *state)
{
	app_todo_snapshot_t todo = { 0 };
	(void)sync_service_get_todo_snapshot(&todo);
	const app_todo_item_t *item = ui_todo_item_at(&todo, *state->todo_selected);
	if (item == NULL) {
		*state->view = UI_VIEW_MAIN;
		return;
	}

	if (*state->focus == 0U) {
		int ret = ui_actions_publish_todo(APP_BUS_EVENT_TODO_COMPLETE_REQUEST, item->id);
		ESP_LOGI(TAG, "todo toggle requested id=%s done=%d ret=%d", item->id, item->done ? 0 : 1, ret);
		*state->view = UI_VIEW_MAIN;
		*state->focus = 0;
	} else {
		*state->view = UI_VIEW_TODO_DELETE_CONFIRM;
		*state->focus = 0;
	}
}

static void process_todo_delete_confirm(const ui_settings_controller_state_t *state)
{
	app_todo_snapshot_t todo = { 0 };
	(void)sync_service_get_todo_snapshot(&todo);
	const app_todo_item_t *item = ui_todo_item_at(&todo, *state->todo_selected);
	if (*state->focus == 0U && item != NULL) {
		int ret = ui_actions_publish_todo(APP_BUS_EVENT_TODO_DELETE_REQUEST, item->id);
		ESP_LOGI(TAG, "todo delete requested id=%s ret=%d", item->id, ret);
		*state->view = UI_VIEW_MAIN;
		*state->focus = 0;
	} else {
		*state->view = UI_VIEW_TODO_ITEM;
		*state->focus = 0;
	}
}

static void process_low_settings(const ui_settings_controller_state_t *state)
{
	if (*state->focus == 0U) {
		*state->low_use_24h = !*state->low_use_24h;
	} else if (*state->focus == 1U) {
		static const uint16_t values[] = { 10, 30, 60, 120, 300 };
		(void)ui_settings_cycle_u16(state->low_enter_absent_s, values, sizeof(values) / sizeof(values[0]), 60);
	} else {
		static const uint16_t values[] = { 1, 3, 5, 10, 30 };
		(void)ui_settings_cycle_u16(state->low_exit_present_s, values, sizeof(values) / sizeof(values[0]), 3);
	}
	state->save_settings();
}

void ui_settings_controller_process_ok(const ui_settings_controller_state_t *state)
{
	if (state == NULL || state->view == NULL || state->focus == NULL) {
		return;
	}

	switch (*state->view) {
	case UI_VIEW_HOME_SETTINGS:
		process_home_settings(state);
		break;
	case UI_VIEW_ALARM_SETTINGS:
		process_alarm_settings(state);
		break;
	case UI_VIEW_ALARM_ITEM:
		process_alarm_item(state);
		break;
	case UI_VIEW_TODO_SETTINGS:
		process_todo_settings(state);
		break;
	case UI_VIEW_TODO_ITEM:
		process_todo_item(state);
		break;
	case UI_VIEW_TODO_DELETE_CONFIRM:
		process_todo_delete_confirm(state);
		break;
	case UI_VIEW_ENV_SETTINGS:
		ui_settings_adjust_process_env(state);
		break;
	case UI_VIEW_LOW_SETTINGS:
		process_low_settings(state);
		break;
	default:
		break;
	}
}
