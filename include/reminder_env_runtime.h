#ifndef REMINDER_ENV_RUNTIME_H_
#define REMINDER_ENV_RUNTIME_H_

#include <stdint.h>

#include <audio_service.h>
#include <settings_model.h>

typedef struct {
	app_audio_event_t active_alert;
	int64_t last_alert_play_us;
} reminder_env_runtime_t;

void reminder_env_runtime_init(reminder_env_runtime_t *runtime);
void reminder_env_runtime_update(reminder_env_runtime_t *runtime, const app_settings_t *settings);

#endif
