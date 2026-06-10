#ifndef APP_UI_FORMAT_H_
#define APP_UI_FORMAT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void ui_format_time_period(char *out, size_t out_size, bool use_24h);
void ui_format_time(char *out, size_t out_size, bool use_24h, bool with_seconds);
void ui_format_date(char *out, size_t out_size);
void ui_format_alarm_time(uint8_t hour, uint8_t minute, char *out, size_t out_size);

#endif
