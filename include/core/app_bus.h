#ifndef APP_BUS_H_
#define APP_BUS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <audio/audio_service.h>
#include <config/settings_model.h>

typedef enum {
	APP_BUS_EVENT_TODO_SYNC_REQUEST = 1,
	APP_BUS_EVENT_TODO_COMPLETE_REQUEST,
	APP_BUS_EVENT_TODO_DELETE_REQUEST,
	APP_BUS_EVENT_ALARM_SETTINGS_CHANGED,
	APP_BUS_EVENT_VOICE_SETTINGS_CHANGED,
	APP_BUS_EVENT_SETTINGS_CHANGED,
	APP_BUS_EVENT_DEVICE_EVENT,
	APP_BUS_EVENT_AUDIO_PLAY_REQUEST,
	APP_BUS_EVENT_INPUT_KEY_PRESSED,
	APP_BUS_EVENT_WEB_CONFIG_UPDATED,
	APP_BUS_EVENT_SYNC_FAILED,
} app_bus_event_type_t;

typedef struct {
	app_bus_event_type_t type;
	int64_t timestamp_us;
	union {
		struct {
			char todo_id[24];
		} todo;
		struct {
			app_settings_t settings;
		} settings;
		struct {
			char event_type[32];
			char todo_id[24];
		} device_event;
		struct {
			app_audio_event_t event_id;
		} audio;
		struct {
			uint8_t key_index;
		} input;
		struct {
			char reason[64];
		} error;
	} data;
} app_bus_event_t;

typedef void (*app_bus_handler_t)(const app_bus_event_t *event, void *ctx);

int app_bus_subscribe(app_bus_event_type_t type, app_bus_handler_t handler, void *ctx);
int app_bus_publish(const app_bus_event_t *event);

#endif
