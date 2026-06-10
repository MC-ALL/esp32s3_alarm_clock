#include <interaction/ui_format.h>

#include <stdio.h>
#include <time.h>

void ui_format_time_period(char *out, size_t out_size, bool use_24h)
{
	time_t now = 0;
	struct tm t = { 0 };
	time(&now);
	localtime_r(&now, &t);

	if (out_size == 0U) {
		return;
	}
	if (t.tm_year < (2024 - 1900) || use_24h) {
		out[0] = '\0';
		return;
	}

	snprintf(out, out_size, t.tm_hour < 12 ? "AM" : "PM");
}

void ui_format_time(char *out, size_t out_size, bool use_24h, bool with_seconds)
{
	time_t now = 0;
	struct tm t = { 0 };
	time(&now);
	localtime_r(&now, &t);

	if (t.tm_year < (2024 - 1900)) {
		snprintf(out, out_size, with_seconds ? "--:--:--" : "--:--");
		return;
	}

	if (use_24h) {
		snprintf(out, out_size, with_seconds ? "%02d:%02d:%02d" : "%02d:%02d",
			 t.tm_hour, t.tm_min, t.tm_sec);
		return;
	}

	int hour = t.tm_hour % 12;
	if (hour == 0) {
		hour = 12;
	}
	snprintf(out, out_size, with_seconds ? "%02d:%02d:%02d" : "%02d:%02d", hour, t.tm_min, t.tm_sec);
}

void ui_format_date(char *out, size_t out_size)
{
	time_t now = 0;
	struct tm t = { 0 };
	time(&now);
	localtime_r(&now, &t);

	if (t.tm_year < (2024 - 1900)) {
		snprintf(out, out_size, "NO TIME");
		return;
	}

	snprintf(out, out_size, "%02d/%02d", t.tm_mon + 1, t.tm_mday);
}

void ui_format_alarm_time(uint8_t hour, uint8_t minute, char *out, size_t out_size)
{
	snprintf(out, out_size, "%02u:%02u", (unsigned)hour, (unsigned)minute);
}
