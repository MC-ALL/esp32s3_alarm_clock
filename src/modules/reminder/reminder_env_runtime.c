#include <reminder/reminder_env_runtime.h>

#include <sensing/environment_service.h>
#include <reminder/reminder_env_alert.h>
#include <reminder/reminder_events.h>

#include <esp_log.h>
#include <esp_timer.h>
#include <stdbool.h>

static const char *TAG = "reminder_env";

void reminder_env_runtime_init(reminder_env_runtime_t *runtime)
{
	if (runtime == NULL) {
		return;
	}
	*runtime = (reminder_env_runtime_t){
		.active_alert = APP_AUDIO_EVENT_TEST,
	};
}

void reminder_env_runtime_update(reminder_env_runtime_t *runtime, const app_settings_t *settings)
{
	if (runtime == NULL) {
		return;
	}

	if (settings == NULL || !settings->env_alert_on) {
		runtime->active_alert = APP_AUDIO_EVENT_TEST;
		return;
	}

	app_environment_snapshot_t env = { 0 };
	if (!environment_service_get_snapshot(&env)) {
		runtime->active_alert = APP_AUDIO_EVENT_TEST;
		return;
	}

	app_audio_event_t event = APP_AUDIO_EVENT_TEST;
	if (!reminder_env_alert_event(&env, settings, &event)) {
		if (runtime->active_alert != APP_AUDIO_EVENT_TEST) {
			ESP_LOGI(TAG, "env alert cleared");
		}
		runtime->active_alert = APP_AUDIO_EVENT_TEST;
		return;
	}

	const int64_t now_us = esp_timer_get_time();
	const bool changed = event != runtime->active_alert;
	const bool repeat_due = (now_us - runtime->last_alert_play_us) >= 60000000LL;
	if (!changed && !repeat_due) {
		return;
	}

	ESP_LOGW(TAG,
		 "env alert %s temp=%.1f[%d,%d] humi=%.1f[%u,%u] lux=%.1f[%u,%u] voice=%d",
		 reminder_env_alert_name(event),
		 env.temperature_c,
		 (int)settings->env_temp_low_c,
		 (int)settings->env_temp_high_c,
		 env.humidity_percent,
		 (unsigned)settings->env_humi_low_percent,
		 (unsigned)settings->env_humi_high_percent,
		 env.lux,
		 (unsigned)settings->env_lux_low,
		 (unsigned)settings->env_lux_high,
		 settings->env_voice_on ? 1 : 0);
	if (settings->env_voice_on) {
		int ret = reminder_events_request_audio(event);
		ESP_LOGI(TAG, "env voice event=%s ret=%d", reminder_env_alert_name(event), ret);
		if (ret == 0) {
			reminder_events_publish_device("env_alert_triggered", NULL);
		}
	}
	runtime->last_alert_play_us = now_us;
	runtime->active_alert = event;
}
