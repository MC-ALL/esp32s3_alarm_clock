#include <interaction/ui_actions.h>

#include <esp_log.h>
#include <string.h>

static const char *TAG = "ui_actions";

void ui_actions_publish_settings(app_bus_event_type_t type, const app_settings_t *settings)
{
	if (settings == NULL) {
		return;
	}

	app_bus_event_t event = {
		.type = type,
	};
	event.data.settings.settings = *settings;
	if (app_bus_publish(&event) != 0) {
		ESP_LOGW(TAG, "publish settings event failed type=%d", (int)type);
	}
}

int ui_actions_request_todo_sync(void)
{
	app_bus_event_t event = {
		.type = APP_BUS_EVENT_TODO_SYNC_REQUEST,
	};
	int ret = app_bus_publish(&event);
	ESP_LOGI(TAG, "todo sync requested ret=%d", ret);
	return ret;
}

int ui_actions_publish_todo(app_bus_event_type_t type, const char *todo_id)
{
	if (todo_id == NULL || todo_id[0] == '\0') {
		return -1;
	}
	app_bus_event_t event = {
		.type = type,
	};
	strlcpy(event.data.todo.todo_id, todo_id, sizeof(event.data.todo.todo_id));
	return app_bus_publish(&event);
}

int ui_actions_publish_device_event(const char *event_type, const char *todo_id)
{
	if (event_type == NULL || event_type[0] == '\0') {
		return -1;
	}
	app_bus_event_t event = {
		.type = APP_BUS_EVENT_DEVICE_EVENT,
	};
	strlcpy(event.data.device_event.event_type, event_type, sizeof(event.data.device_event.event_type));
	if (todo_id != NULL) {
		strlcpy(event.data.device_event.todo_id, todo_id, sizeof(event.data.device_event.todo_id));
	}
	return app_bus_publish(&event);
}

static void play_voice_test(const app_audio_event_t *events, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		app_bus_event_t event = {
			.type = APP_BUS_EVENT_AUDIO_PLAY_REQUEST,
		};
		event.data.audio.event_id = events[i];
		int ret = app_bus_publish(&event);
		ESP_LOGI(TAG, "voice test event=%d ret=%d", (int)events[i], ret);
	}
}

void ui_actions_play_alarm_voice_test(void)
{
	const app_audio_event_t events[] = { APP_AUDIO_EVENT_ALARM };
	play_voice_test(events, sizeof(events) / sizeof(events[0]));
}

void ui_actions_play_todo_voice_test(void)
{
	const app_audio_event_t events[] = { APP_AUDIO_EVENT_TODO_SYNC_UP };
	play_voice_test(events, sizeof(events) / sizeof(events[0]));
}

void ui_actions_play_env_voice_test(void)
{
	const app_audio_event_t events[] = {
		APP_AUDIO_EVENT_ENV_LIGHT_LOW,
		APP_AUDIO_EVENT_ENV_LIGHT_HIGH,
		APP_AUDIO_EVENT_ENV_TEMP_LOW,
		APP_AUDIO_EVENT_ENV_TEMP_HIGH,
		APP_AUDIO_EVENT_ENV_HUMI_LOW,
		APP_AUDIO_EVENT_ENV_HUMI_HIGH,
	};
	play_voice_test(events, sizeof(events) / sizeof(events[0]));
}
