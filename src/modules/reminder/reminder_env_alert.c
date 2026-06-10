#include <reminder_env_alert.h>

const char *reminder_env_alert_name(app_audio_event_t event)
{
	switch (event) {
	case APP_AUDIO_EVENT_ENV_LIGHT_LOW:
		return "light_low";
	case APP_AUDIO_EVENT_ENV_LIGHT_HIGH:
		return "light_high";
	case APP_AUDIO_EVENT_ENV_TEMP_LOW:
		return "temp_low";
	case APP_AUDIO_EVENT_ENV_TEMP_HIGH:
		return "temp_high";
	case APP_AUDIO_EVENT_ENV_HUMI_LOW:
		return "humi_low";
	case APP_AUDIO_EVENT_ENV_HUMI_HIGH:
		return "humi_high";
	default:
		return "none";
	}
}

bool reminder_env_alert_event(const app_environment_snapshot_t *env, const app_settings_t *settings,
			      app_audio_event_t *event)
{
	if (env == NULL || settings == NULL || event == NULL) {
		return false;
	}

	if (env->dht11_valid) {
		if (env->temperature_c < (float)settings->env_temp_low_c) {
			*event = APP_AUDIO_EVENT_ENV_TEMP_LOW;
			return true;
		}
		if (env->temperature_c > (float)settings->env_temp_high_c) {
			*event = APP_AUDIO_EVENT_ENV_TEMP_HIGH;
			return true;
		}
		if (env->humidity_percent < (float)settings->env_humi_low_percent) {
			*event = APP_AUDIO_EVENT_ENV_HUMI_LOW;
			return true;
		}
		if (env->humidity_percent > (float)settings->env_humi_high_percent) {
			*event = APP_AUDIO_EVENT_ENV_HUMI_HIGH;
			return true;
		}
	}

	if (env->bh1750_valid) {
		if (env->lux < (float)settings->env_lux_low) {
			*event = APP_AUDIO_EVENT_ENV_LIGHT_LOW;
			return true;
		}
		if (env->lux > (float)settings->env_lux_high) {
			*event = APP_AUDIO_EVENT_ENV_LIGHT_HIGH;
			return true;
		}
	}

	return false;
}
