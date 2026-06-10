#ifndef REMINDER_ENV_ALERT_H_
#define REMINDER_ENV_ALERT_H_

#include <audio/audio_service.h>
#include <sensing/environment_service.h>
#include <config/settings_model.h>

const char *reminder_env_alert_name(app_audio_event_t event);
bool reminder_env_alert_event(const app_environment_snapshot_t *env, const app_settings_t *settings,
			      app_audio_event_t *event);

#endif
