#include <reminder/reminder_rest_runtime.h>

#define APP_REST_REMINDER_PRESENT_US (3LL * 60LL * 60LL * 1000000LL)

bool reminder_rest_runtime_update(reminder_rest_runtime_t *runtime, const app_presence_status_t *presence,
				  int64_t now_us, int64_t *present_us)
{
	if (runtime == NULL || presence == NULL) {
		return false;
	}

	if (!presence->detected) {
		runtime->present_since_us = 0;
		runtime->fired = false;
		return false;
	}

	if (runtime->present_since_us == 0) {
		runtime->present_since_us = now_us;
		runtime->fired = false;
		return false;
	}

	if (runtime->fired) {
		return false;
	}

	const int64_t elapsed_us = now_us - runtime->present_since_us;
	if (elapsed_us < APP_REST_REMINDER_PRESENT_US) {
		return false;
	}

	if (present_us != NULL) {
		*present_us = elapsed_us;
	}
	return true;
}

void reminder_rest_runtime_mark_fired(reminder_rest_runtime_t *runtime)
{
	if (runtime != NULL) {
		runtime->fired = true;
	}
}
