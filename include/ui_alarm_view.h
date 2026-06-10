#ifndef APP_UI_ALARM_VIEW_H_
#define APP_UI_ALARM_VIEW_H_

#include <stdint.h>

#include <ui_types.h>

const ui_alarm_item_t *ui_alarm_next_enabled(const ui_alarm_item_t *alarms, uint8_t alarm_count);

#endif
