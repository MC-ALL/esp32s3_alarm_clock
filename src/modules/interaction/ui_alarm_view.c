#include <ui_alarm_view.h>

const ui_alarm_item_t *ui_alarm_next_enabled(const ui_alarm_item_t *alarms, uint8_t alarm_count)
{
	if (alarms == NULL) {
		return NULL;
	}

	for (uint8_t i = 0; i < alarm_count; i++) {
		if (alarms[i].enabled) {
			return &alarms[i];
		}
	}
	return NULL;
}
