#ifndef REMINDER_TIME_RUNTIME_H_
#define REMINDER_TIME_RUNTIME_H_

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include <config/settings_model.h>

typedef struct {
	int alarm_last_yday;
	int alarm_last_minute;
	int hour_chime_last_yday;
	int hour_chime_last_hour;
} reminder_time_runtime_t;

typedef struct {
	uint8_t index;
	app_alarm_setting_t alarm;
} reminder_alarm_fire_t;

void reminder_time_runtime_init(reminder_time_runtime_t *runtime);
bool reminder_time_runtime_update_alarm(reminder_time_runtime_t *runtime,
					const app_settings_t *settings,
					const struct tm *time_now,
					reminder_alarm_fire_t *fire);
bool reminder_time_runtime_update_hour_chime(reminder_time_runtime_t *runtime,
					     const app_settings_t *settings,
					     const struct tm *time_now);

#endif
