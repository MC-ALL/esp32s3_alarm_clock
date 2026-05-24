#include "app_module.h"
#include <app/audio_service.h>
#include <app/environment_service.h>
#include <app/module_common.h>
#include <app/net_service.h>
#include <app/settings_model.h>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>

static const char *TAG = "reminder";

static TaskHandle_t s_reminder_task;
static int s_alarm_last_yday = -1;
static int s_alarm_last_minute = -1;
static int s_hour_chime_last_yday = -1;
static int s_hour_chime_last_hour = -1;
static app_audio_event_t s_env_active_alert = APP_AUDIO_EVENT_TEST;
static int64_t s_env_last_alert_play_us;
static bool s_todo_seen_once;
static char s_known_todo_ids[APP_TODO_MAX_ITEMS][24];
static uint8_t s_known_todo_count;

static const char *env_alert_name(app_audio_event_t event)
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

static bool todo_id_known(const char *id)
{
	for (uint8_t i = 0; i < s_known_todo_count; i++) {
		if (strcmp(s_known_todo_ids[i], id) == 0) {
			return true;
		}
	}
	return false;
}

static void remember_todo_ids(const app_todo_snapshot_t *todo)
{
	s_known_todo_count = 0;
	for (uint8_t i = 0; i < todo->count && i < APP_TODO_MAX_ITEMS; i++) {
		if (todo->items[i].id[0] == '\0') {
			continue;
		}
		strlcpy(s_known_todo_ids[s_known_todo_count], todo->items[i].id, sizeof(s_known_todo_ids[0]));
		s_known_todo_count++;
	}
}

static bool env_snapshot_alert_event(const app_environment_snapshot_t *env, const app_settings_t *settings,
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

static void update_alarm_runtime(app_settings_t *settings, const struct tm *t)
{
	if (settings == NULL || t == NULL || t->tm_year < (2024 - 1900) || t->tm_sec > 2) {
		return;
	}

	const int minute_of_day = t->tm_hour * 60 + t->tm_min;
	if (s_alarm_last_yday == t->tm_yday && s_alarm_last_minute == minute_of_day) {
		return;
	}

	for (uint8_t i = 0; i < settings->alarm_count && i < APP_SETTINGS_MAX_ALARMS; i++) {
		app_alarm_setting_t *alarm = &settings->alarms[i];
		if (!alarm->enabled || alarm->hour != (uint8_t)t->tm_hour || alarm->minute != (uint8_t)t->tm_min) {
			continue;
		}

		s_alarm_last_yday = t->tm_yday;
		s_alarm_last_minute = minute_of_day;
		if (settings->alarm_voice_on && alarm->voice) {
			int ret = audio_service_play_event(APP_AUDIO_EVENT_ALARM);
			ESP_LOGI(TAG, "alarm fired index=%u time=%02u:%02u ret=%d",
				 (unsigned)i, (unsigned)alarm->hour, (unsigned)alarm->minute, ret);
		} else {
			ESP_LOGI(TAG, "alarm fired index=%u time=%02u:%02u voice=0",
				 (unsigned)i, (unsigned)alarm->hour, (unsigned)alarm->minute);
		}
		if (!alarm->repeat) {
			alarm->enabled = false;
			(void)settings_model_set(settings);
		}
		return;
	}
}

static void update_hour_chime_runtime(const app_settings_t *settings, const struct tm *t)
{
	if (settings == NULL || t == NULL || !settings->home_hour_chime_on ||
	    t->tm_year < (2024 - 1900) || t->tm_min != 0 || t->tm_sec > 2) {
		return;
	}
	if (s_hour_chime_last_yday == t->tm_yday && s_hour_chime_last_hour == t->tm_hour) {
		return;
	}

	s_hour_chime_last_yday = t->tm_yday;
	s_hour_chime_last_hour = t->tm_hour;
	int ret = audio_service_play_event(APP_AUDIO_EVENT_HOUR_CHIME);
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
	if (!env_snapshot_alert_event(&env, settings, &event)) {
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
			 env_alert_name(event),
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
			int ret = audio_service_play_event(event);
			ESP_LOGI(TAG, "env voice event=%s ret=%d", env_alert_name(event), ret);
		}
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
	if (!net_service_get_todo_snapshot(&todo) || !todo.sync_ok || todo.sync_in_progress) {
		return;
	}

	uint8_t new_count = 0;
	for (uint8_t i = 0; i < todo.count && i < APP_TODO_MAX_ITEMS; i++) {
		const app_todo_item_t *item = &todo.items[i];
		if (item->done || item->id[0] == '\0') {
			continue;
		}
		if (s_todo_seen_once && !todo_id_known(item->id)) {
			new_count++;
			ESP_LOGI(TAG, "new todo detected id=%s text=%.32s", item->id, item->text);
		}
	}

	remember_todo_ids(&todo);
	if (!s_todo_seen_once) {
		s_todo_seen_once = true;
		return;
	}

	if (new_count > 0U) {
		ESP_LOGI(TAG, "new todo alert count=%u voice=%d", (unsigned)new_count, settings->todo_voice_on ? 1 : 0);
		if (settings->todo_voice_on) {
			int ret = audio_service_play_event(APP_AUDIO_EVENT_REST_REMINDER);
			ESP_LOGI(TAG, "todo voice ret=%d", ret);
		}
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

		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}

int reminder_service_init(void)
{
	ESP_LOGI(TAG, "init");
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
