#include "app_module.h"
#include <environment_service.h>
#include <module_common.h>
#include <presence_service.h>
#include <reminder_env_alert.h>
#include <reminder_events.h>
#include <reminder_rest_runtime.h>
#include <reminder_time_runtime.h>
#include <reminder_todo_runtime.h>
#include <settings_model.h>
#include <sync_service.h>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <inttypes.h>
#include <time.h>

static const char *TAG = "reminder";

static TaskHandle_t s_reminder_task;
static app_audio_event_t s_env_active_alert = APP_AUDIO_EVENT_TEST;
static int64_t s_env_last_alert_play_us;
static reminder_time_runtime_t s_time_runtime;
static reminder_todo_runtime_t s_todo_runtime;
static reminder_rest_runtime_t s_rest_runtime;

static void update_alarm_runtime(app_settings_t *settings, const struct tm *t)
{
	if (settings == NULL) {
		return;
	}

	reminder_alarm_fire_t fire = { 0 };
	if (!reminder_time_runtime_update_alarm(&s_time_runtime, settings, t, &fire)) {
		return;
	}

	reminder_events_publish_device("alarm_triggered", NULL);
	if (settings->alarm_voice_on && fire.alarm.voice) {
		int ret = reminder_events_request_audio(APP_AUDIO_EVENT_ALARM);
		ESP_LOGI(TAG, "alarm fired index=%u time=%02u:%02u ret=%d",
			 (unsigned)fire.index, (unsigned)fire.alarm.hour, (unsigned)fire.alarm.minute, ret);
	} else {
		ESP_LOGI(TAG, "alarm fired index=%u time=%02u:%02u voice=0",
			 (unsigned)fire.index, (unsigned)fire.alarm.hour, (unsigned)fire.alarm.minute);
	}
	if (!fire.alarm.repeat && fire.index < settings->alarm_count && fire.index < APP_SETTINGS_MAX_ALARMS) {
		settings->alarms[fire.index].enabled = false;
		if (settings_model_set(settings) == 0) {
			reminder_events_publish_alarm_settings_changed(settings);
		}
	}
}

static void update_hour_chime_runtime(const app_settings_t *settings, const struct tm *t)
{
	if (!reminder_time_runtime_update_hour_chime(&s_time_runtime, settings, t)) {
		return;
	}

	int ret = reminder_events_request_audio(APP_AUDIO_EVENT_HOUR_CHIME);
	ESP_LOGI(TAG, "hour chime fired hour=%02d ret=%d", t->tm_hour, ret);
}

static void update_env_alert_runtime(const app_settings_t *settings)
{
	if (settings == NULL || !settings->env_alert_on) {
		s_env_active_alert = APP_AUDIO_EVENT_TEST;
		return;
	}

	app_environment_snapshot_t env = { 0 };
	if (!environment_service_get_snapshot(&env)) {
		s_env_active_alert = APP_AUDIO_EVENT_TEST;
		return;
	}

	app_audio_event_t event = APP_AUDIO_EVENT_TEST;
	if (!reminder_env_alert_event(&env, settings, &event)) {
		if (s_env_active_alert != APP_AUDIO_EVENT_TEST) {
			ESP_LOGI(TAG, "env alert cleared");
		}
		s_env_active_alert = APP_AUDIO_EVENT_TEST;
		return;
	}

	const int64_t now_us = esp_timer_get_time();
	const bool changed = event != s_env_active_alert;
	const bool repeat_due = (now_us - s_env_last_alert_play_us) >= 60000000LL;
	if (changed || repeat_due) {
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
		}
		reminder_events_publish_device("env_alert_triggered", NULL);
		s_env_last_alert_play_us = now_us;
		s_env_active_alert = event;
	}
}

static void update_todo_runtime(const app_settings_t *settings)
{
	if (settings == NULL) {
		return;
	}

	app_todo_snapshot_t todo = { 0 };
	if (!sync_service_get_todo_snapshot(&todo)) {
		return;
	}

	uint8_t new_count = reminder_todo_runtime_update(&s_todo_runtime, &todo);
	if (new_count > 0U) {
		ESP_LOGI(TAG, "new todo alert count=%u voice=%d", (unsigned)new_count, settings->todo_voice_on ? 1 : 0);
		if (settings->todo_voice_on) {
			int ret = reminder_events_request_audio(APP_AUDIO_EVENT_TODO_SYNC_UP);
			ESP_LOGI(TAG, "todo voice ret=%d", ret);
			if (ret == 0) {
				reminder_events_publish_device("todo_sync_up_played", NULL);
			}
		}
	}
}

static void update_rest_reminder_runtime(void)
{
	app_presence_status_t presence = { 0 };
	if (!presence_service_get_status(&presence)) {
		return;
	}

	const int64_t now_us = esp_timer_get_time();
	int64_t present_us = 0;
	if (!reminder_rest_runtime_update(&s_rest_runtime, &presence, now_us, &present_us)) {
		return;
	}

	int ret = reminder_events_request_audio(APP_AUDIO_EVENT_REST_REMINDER);
	ESP_LOGI(TAG, "rest reminder fired present_s=%" PRIi64 " ret=%d",
		 present_us / 1000000LL, ret);
	if (ret == 0) {
		reminder_rest_runtime_mark_fired(&s_rest_runtime);
		reminder_events_publish_device("rest_reminder_triggered", NULL);
	}
}

static void reminder_task(void *arg)
{
	(void)arg;

	for (;;) {
		time_t now = 0;
		struct tm t = { 0 };
		app_settings_t settings = { 0 };

		time(&now);
		localtime_r(&now, &t);
		settings_model_get(&settings);

		update_alarm_runtime(&settings, &t);
		update_hour_chime_runtime(&settings, &t);
		update_env_alert_runtime(&settings);
		update_todo_runtime(&settings);
		update_rest_reminder_runtime();

		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}

int reminder_service_init(void)
{
	ESP_LOGI(TAG, "init");
	reminder_time_runtime_init(&s_time_runtime);
	return 0;
}

int reminder_service_start(void)
{
	BaseType_t ok = xTaskCreate(reminder_task, "reminder_task", 4096, NULL, 6, &s_reminder_task);
	if (ok != pdPASS) {
		ESP_LOGE(TAG, "failed to create reminder task");
		return -1;
	}

	return 0;
}

int reminder_service_stop(void)
{
	if (s_reminder_task != NULL) {
		vTaskDelete(s_reminder_task);
		s_reminder_task = NULL;
	}

	return 0;
}
