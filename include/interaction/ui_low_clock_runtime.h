#ifndef UI_LOW_CLOCK_RUNTIME_H_
#define UI_LOW_CLOCK_RUNTIME_H_

#include <stdbool.h>
#include <stdint.h>

#include <sensing/presence_service.h>
#include <interaction/ui_types.h>

typedef enum {
	UI_LOW_CLOCK_ACTION_NONE = 0,
	UI_LOW_CLOCK_ACTION_ENTER,
	UI_LOW_CLOCK_ACTION_EXIT,
} ui_low_clock_action_t;

typedef struct {
	bool auto_entered;
	int64_t absent_since_us;
	int64_t present_since_us;
} ui_low_clock_runtime_t;

typedef struct {
	app_presence_status_t presence;
	int64_t duration_s;
	uint16_t threshold_s;
} ui_low_clock_result_t;

ui_low_clock_action_t ui_low_clock_runtime_update(ui_low_clock_runtime_t *runtime,
						  ui_main_page_t *main_page,
						  ui_view_t view,
						  uint16_t enter_absent_s,
						  uint16_t exit_present_s,
						  ui_low_clock_result_t *result);

#endif
