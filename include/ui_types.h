#ifndef APP_UI_TYPES_H_
#define APP_UI_TYPES_H_

#include <stdbool.h>
#include <stdint.h>

#define UI_MAX_ALARMS 5U
#define UI_MAIN_PAGE_COUNT 6U
#define UI_KEY_COUNT 4U

typedef enum {
	UI_PAGE_HOME = 0,
	UI_PAGE_ALARM,
	UI_PAGE_TODO,
	UI_PAGE_ENV,
	UI_PAGE_WIFI,
	UI_PAGE_LOW_CLOCK,
} ui_main_page_t;

typedef enum {
	UI_VIEW_MAIN = 0,
	UI_VIEW_HOME_SETTINGS,
	UI_VIEW_ALARM_SETTINGS,
	UI_VIEW_ALARM_ITEM,
	UI_VIEW_TODO_SETTINGS,
	UI_VIEW_TODO_ITEM,
	UI_VIEW_TODO_DELETE_CONFIRM,
	UI_VIEW_ENV_SETTINGS,
	UI_VIEW_LOW_SETTINGS,
} ui_view_t;

typedef enum {
	UI_KEY_HOME_BACK = 0,
	UI_KEY_PREV_UP,
	UI_KEY_NEXT_DOWN,
	UI_KEY_OK,
} ui_key_t;

typedef struct {
	uint8_t hour;
	uint8_t minute;
	bool repeat;
	bool enabled;
	bool voice;
} ui_alarm_item_t;

#endif
