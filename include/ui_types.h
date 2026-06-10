#ifndef APP_UI_TYPES_H_
#define APP_UI_TYPES_H_

#include <stdbool.h>
#include <stdint.h>

#define UI_MAX_ALARMS 5U
#define UI_MAIN_PAGE_COUNT 6U
#define UI_KEY_COUNT 4U

typedef struct {
	uint8_t hour;
	uint8_t minute;
	bool repeat;
	bool enabled;
	bool voice;
} ui_alarm_item_t;

#endif
