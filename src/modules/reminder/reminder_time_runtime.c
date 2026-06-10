#include <reminder/reminder_time_runtime.h>

#include <stddef.h>

void reminder_time_runtime_init(reminder_time_runtime_t *runtime)
{
	if (runtime == NULL) {
		return;
	}
	*runtime = (reminder_time_runtime_t){
		.alarm_last_yday = -1,
		.alarm_last_minute = -1,
		.hour_chime_last_yday = -1,
		.hour_chime_last_hour = -1,
	};
}

bool reminder_time_runtime_update_alarm(reminder_time_runtime_t *runtime,
					const app_settings_t *settings,
					const struct tm *time_now,
					reminder_alarm_fire_t *fire)
{
	if (runtime == NULL || settings == NULL || time_now == NULL ||
	    time_now->tm_year < (2024 - 1900) || time_now->tm_sec > 2) {
		return false;
	}

	const int minute_of_day = time_now->tm_hour * 60 + time_now->tm_min;
	if (runtime->alarm_last_yday == time_now->tm_yday &&
	    runtime->alarm_last_minute == minute_of_day) {
		return false;
	}

	for (uint8_t i = 0; i < settings->alarm_count && i < APP_SETTINGS_MAX_ALARMS; i++) {
		const app_alarm_setting_t *alarm = &settings->alarms[i];
		if (!alarm->enabled || alarm->hour != (uint8_t)time_now->tm_hour ||
		    alarm->minute != (uint8_t)time_now->tm_min) {
			continue;
		}

		runtime->alarm_last_yday = time_now->tm_yday;
		runtime->alarm_last_minute = minute_of_day;
		if (fire != NULL) {
			*fire = (reminder_alarm_fire_t){
				.index = i,
				.alarm = *alarm,
			};
		}
		return true;
	}

	return false;
}

bool reminder_time_runtime_update_hour_chime(reminder_time_runtime_t *runtime,
					     const app_settings_t *settings,
					     const struct tm *time_now)
{
	if (runtime == NULL || settings == NULL || time_now == NULL ||
	    !settings->home_hour_chime_on || time_now->tm_year < (2024 - 1900) ||
	    time_now->tm_min != 0 || time_now->tm_sec > 2) {
		return false;
	}
	if (runtime->hour_chime_last_yday == time_now->tm_yday &&
	    runtime->hour_chime_last_hour == time_now->tm_hour) {
		return false;
	}

	runtime->hour_chime_last_yday = time_now->tm_yday;
	runtime->hour_chime_last_hour = time_now->tm_hour;
	return true;
}
