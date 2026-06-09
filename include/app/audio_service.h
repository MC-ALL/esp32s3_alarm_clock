#ifndef APP_AUDIO_SERVICE_H_
#define APP_AUDIO_SERVICE_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	APP_AUDIO_EVENT_TEST = 0,
	APP_AUDIO_EVENT_WELCOME,
	APP_AUDIO_EVENT_ENV_LIGHT_LOW,
	APP_AUDIO_EVENT_ENV_LIGHT_HIGH,
	APP_AUDIO_EVENT_ENV_TEMP_LOW,
	APP_AUDIO_EVENT_ENV_TEMP_HIGH,
	APP_AUDIO_EVENT_ENV_HUMI_LOW,
	APP_AUDIO_EVENT_ENV_HUMI_HIGH,
	APP_AUDIO_EVENT_CONFIRM,
	APP_AUDIO_EVENT_TODO_SYNC_UP,
	APP_AUDIO_EVENT_REST_REMINDER,
	APP_AUDIO_EVENT_ALARM,
	APP_AUDIO_EVENT_HOUR_CHIME,
	APP_AUDIO_EVENT_WIFI_CONNECTED,
} app_audio_event_t;

int audio_service_play_test_tone(uint32_t frequency_hz, uint32_t duration_ms);
int audio_service_play_event(app_audio_event_t event_id);
bool audio_service_is_busy(void);
int audio_service_set_volume(uint8_t volume);
uint8_t audio_service_get_volume(void);

#endif
