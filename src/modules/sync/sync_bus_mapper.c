#include <sync_bus_mapper.h>

#include <string.h>

bool sync_bus_mapper_event_to_request(const app_bus_event_t *event, sync_request_t *request)
{
	if (event == NULL || request == NULL) {
		return false;
	}

	*request = (sync_request_t){ 0 };
	if (event->type == APP_BUS_EVENT_TODO_SYNC_REQUEST) {
		request->type = SYNC_REQUEST_PULL_CONFIG;
		return true;
	}
	if (event->type == APP_BUS_EVENT_TODO_COMPLETE_REQUEST) {
		request->type = SYNC_REQUEST_COMPLETE_TODO;
		strlcpy(request->todo_id, event->data.todo.todo_id, sizeof(request->todo_id));
		return true;
	}
	if (event->type == APP_BUS_EVENT_TODO_DELETE_REQUEST) {
		request->type = SYNC_REQUEST_DELETE_TODO;
		strlcpy(request->todo_id, event->data.todo.todo_id, sizeof(request->todo_id));
		return true;
	}
	if (event->type == APP_BUS_EVENT_ALARM_SETTINGS_CHANGED) {
		request->type = SYNC_REQUEST_PUSH_ALARMS;
		request->settings = event->data.settings.settings;
		return true;
	}
	if (event->type == APP_BUS_EVENT_VOICE_SETTINGS_CHANGED) {
		request->type = SYNC_REQUEST_PUSH_VOICE;
		request->settings = event->data.settings.settings;
		return true;
	}
	if (event->type == APP_BUS_EVENT_DEVICE_EVENT) {
		request->type = SYNC_REQUEST_REPORT_EVENT;
		strlcpy(request->event_type, event->data.device_event.event_type, sizeof(request->event_type));
		strlcpy(request->todo_id, event->data.device_event.todo_id, sizeof(request->todo_id));
		return true;
	}

	return false;
}
