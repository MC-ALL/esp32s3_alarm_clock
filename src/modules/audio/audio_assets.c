#include <audio_assets.h>

extern const uint8_t welcome_wav_start[] asm("_binary_welcome_wav_start");
extern const uint8_t welcome_wav_end[] asm("_binary_welcome_wav_end");
extern const uint8_t connected_wav_start[] asm("_binary_connected_wav_start");
extern const uint8_t connected_wav_end[] asm("_binary_connected_wav_end");
extern const uint8_t env_light_low_wav_start[] asm("_binary_env_light_low_wav_start");
extern const uint8_t env_light_low_wav_end[] asm("_binary_env_light_low_wav_end");
extern const uint8_t env_light_high_wav_start[] asm("_binary_env_light_high_wav_start");
extern const uint8_t env_light_high_wav_end[] asm("_binary_env_light_high_wav_end");
extern const uint8_t env_temp_low_wav_start[] asm("_binary_env_temp_low_wav_start");
extern const uint8_t env_temp_low_wav_end[] asm("_binary_env_temp_low_wav_end");
extern const uint8_t env_temp_high_wav_start[] asm("_binary_env_temp_high_wav_start");
extern const uint8_t env_temp_high_wav_end[] asm("_binary_env_temp_high_wav_end");
extern const uint8_t env_humi_low_wav_start[] asm("_binary_env_humi_low_wav_start");
extern const uint8_t env_humi_low_wav_end[] asm("_binary_env_humi_low_wav_end");
extern const uint8_t env_humi_high_wav_start[] asm("_binary_env_humi_high_wav_start");
extern const uint8_t env_humi_high_wav_end[] asm("_binary_env_humi_high_wav_end");
extern const uint8_t sync_up_wav_start[] asm("_binary_sync_up_wav_start");
extern const uint8_t sync_up_wav_end[] asm("_binary_sync_up_wav_end");
extern const uint8_t rest_reminder_wav_start[] asm("_binary_rest_reminder_wav_start");
extern const uint8_t rest_reminder_wav_end[] asm("_binary_rest_reminder_wav_end");
extern const uint8_t clock_wav_start[] asm("_binary_clock_wav_start");
extern const uint8_t clock_wav_end[] asm("_binary_clock_wav_end");

static const app_audio_clip_t APP_CLIP_WELCOME = {
	.data = welcome_wav_start,
	.end = welcome_wav_end,
};
static const app_audio_clip_t APP_CLIP_CONNECTED = {
	.data = connected_wav_start,
	.end = connected_wav_end,
};
static const app_audio_clip_t APP_CLIP_ENV_LIGHT_LOW = {
	.data = env_light_low_wav_start,
	.end = env_light_low_wav_end,
};
static const app_audio_clip_t APP_CLIP_ENV_LIGHT_HIGH = {
	.data = env_light_high_wav_start,
	.end = env_light_high_wav_end,
};
static const app_audio_clip_t APP_CLIP_ENV_TEMP_LOW = {
	.data = env_temp_low_wav_start,
	.end = env_temp_low_wav_end,
};
static const app_audio_clip_t APP_CLIP_ENV_TEMP_HIGH = {
	.data = env_temp_high_wav_start,
	.end = env_temp_high_wav_end,
};
static const app_audio_clip_t APP_CLIP_ENV_HUMI_LOW = {
	.data = env_humi_low_wav_start,
	.end = env_humi_low_wav_end,
};
static const app_audio_clip_t APP_CLIP_ENV_HUMI_HIGH = {
	.data = env_humi_high_wav_start,
	.end = env_humi_high_wav_end,
};
static const app_audio_clip_t APP_CLIP_TODO_SYNC_UP = {
	.data = sync_up_wav_start,
	.end = sync_up_wav_end,
};
static const app_audio_clip_t APP_CLIP_REST = {
	.data = rest_reminder_wav_start,
	.end = rest_reminder_wav_end,
};
static const app_audio_clip_t APP_CLIP_ALARM = {
	.data = clock_wav_start,
	.end = clock_wav_end,
};

const app_audio_clip_t *audio_assets_get_clip(app_audio_event_t event_id)
{
	switch (event_id) {
	case APP_AUDIO_EVENT_WELCOME:
		return &APP_CLIP_WELCOME;
	case APP_AUDIO_EVENT_WIFI_CONNECTED:
		return &APP_CLIP_CONNECTED;
	case APP_AUDIO_EVENT_ENV_LIGHT_LOW:
		return &APP_CLIP_ENV_LIGHT_LOW;
	case APP_AUDIO_EVENT_ENV_LIGHT_HIGH:
		return &APP_CLIP_ENV_LIGHT_HIGH;
	case APP_AUDIO_EVENT_ENV_TEMP_LOW:
		return &APP_CLIP_ENV_TEMP_LOW;
	case APP_AUDIO_EVENT_ENV_TEMP_HIGH:
		return &APP_CLIP_ENV_TEMP_HIGH;
	case APP_AUDIO_EVENT_ENV_HUMI_LOW:
		return &APP_CLIP_ENV_HUMI_LOW;
	case APP_AUDIO_EVENT_ENV_HUMI_HIGH:
		return &APP_CLIP_ENV_HUMI_HIGH;
	case APP_AUDIO_EVENT_TODO_SYNC_UP:
		return &APP_CLIP_TODO_SYNC_UP;
	case APP_AUDIO_EVENT_REST_REMINDER:
		return &APP_CLIP_REST;
	case APP_AUDIO_EVENT_ALARM:
	case APP_AUDIO_EVENT_HOUR_CHIME:
		return &APP_CLIP_ALARM;
	case APP_AUDIO_EVENT_CONFIRM:
	case APP_AUDIO_EVENT_TEST:
	default:
		return NULL;
	}
}
