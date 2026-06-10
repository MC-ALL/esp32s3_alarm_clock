#ifndef REMINDER_ENV_ALERT_H_
#define REMINDER_ENV_ALERT_H_

#include <audio_service.h>
#include <environment_service.h>
#include <settings_model.h>

const char *reminder_env_alert_name(app_audio_event_t event);
bool reminder_env_alert_event(const app_environment_snapshot_t *env, const app_settings_t *settings,
			      app_audio_event_t *event);

#endif
