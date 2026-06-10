#include <reminder/reminder_events.h>

#include <core/app_bus.h>

#include <esp_timer.h>
#include <string.h>

void reminder_events_publish_device(const char *event_type, const char *todo_id)
{
	if (event_type == NULL || event_type[0] == '\0') {
		return;
	}
	app_bus_event_t event = {
		.type = APP_BUS_EVENT_DEVICE_EVENT,
		.timestamp_us = esp_timer_get_time(),
	};
	strlcpy(event.data.device_event.event_type, event_type, sizeof(event.data.device_event.event_type));
	if (todo_id != NULL) {
		strlcpy(event.data.device_event.todo_id, todo_id, sizeof(event.data.device_event.todo_id));
	}
	(void)app_bus_publish(&event);
}

void reminder_events_publish_alarm_settings_changed(const app_settings_t *settings)
{
	if (settings == NULL) {
		return;
	}
	app_bus_event_t event = {
		.type = APP_BUS_EVENT_ALARM_SETTINGS_CHANGED,
		.timestamp_us = esp_timer_get_time(),
	};
	event.data.settings.settings = *settings;
	(void)app_bus_publish(&event);
}

int reminder_events_request_audio(app_audio_event_t event_id)
{
	app_bus_event_t event = {
		.type = APP_BUS_EVENT_AUDIO_PLAY_REQUEST,
		.timestamp_us = esp_timer_get_time(),
	};
	event.data.audio.event_id = event_id;
	return app_bus_publish(&event);
}
