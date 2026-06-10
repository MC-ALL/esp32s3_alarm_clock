#include <ui_low_clock_runtime.h>

#include <stddef.h>

#include <esp_timer.h>

ui_low_clock_action_t ui_low_clock_runtime_update(ui_low_clock_runtime_t *runtime,
						  ui_main_page_t *main_page,
						  ui_view_t view,
						  uint16_t enter_absent_s,
						  uint16_t exit_present_s,
						  ui_low_clock_result_t *result)
{
	if (runtime == NULL || main_page == NULL) {
		return UI_LOW_CLOCK_ACTION_NONE;
	}

	app_presence_status_t presence = { 0 };
	if (!presence_service_get_status(&presence)) {
		return UI_LOW_CLOCK_ACTION_NONE;
	}

	const int64_t now_us = esp_timer_get_time();
	if (presence.detected) {
		runtime->absent_since_us = 0;
		if (runtime->present_since_us == 0) {
			runtime->present_since_us = now_us;
		}
	} else {
		runtime->present_since_us = 0;
		if (runtime->absent_since_us == 0) {
			runtime->absent_since_us = now_us;
		}
	}

	if (view != UI_VIEW_MAIN) {
		return UI_LOW_CLOCK_ACTION_NONE;
	}

	if (*main_page != UI_PAGE_LOW_CLOCK && !presence.detected && runtime->absent_since_us > 0) {
		const int64_t absent_s = (now_us - runtime->absent_since_us) / 1000000LL;
		if (absent_s >= (int64_t)enter_absent_s) {
			*main_page = UI_PAGE_LOW_CLOCK;
			runtime->auto_entered = true;
			if (result != NULL) {
				*result = (ui_low_clock_result_t){
					.presence = presence,
					.duration_s = absent_s,
					.threshold_s = enter_absent_s,
				};
			}
			return UI_LOW_CLOCK_ACTION_ENTER;
		}
		return UI_LOW_CLOCK_ACTION_NONE;
	}

	if (*main_page == UI_PAGE_LOW_CLOCK && runtime->auto_entered && presence.detected &&
	    runtime->present_since_us > 0) {
		const int64_t present_s = (now_us - runtime->present_since_us) / 1000000LL;
		if (present_s >= (int64_t)exit_present_s) {
			*main_page = UI_PAGE_HOME;
			runtime->auto_entered = false;
			if (result != NULL) {
				*result = (ui_low_clock_result_t){
					.presence = presence,
					.duration_s = present_s,
					.threshold_s = exit_present_s,
				};
			}
			return UI_LOW_CLOCK_ACTION_EXIT;
		}
	}

	return UI_LOW_CLOCK_ACTION_NONE;
}
