#ifndef REMINDER_REST_RUNTIME_H_
#define REMINDER_REST_RUNTIME_H_

#include <stdbool.h>
#include <stdint.h>

#include <sensing/presence_service.h>

typedef struct {
	int64_t present_since_us;
	bool fired;
} reminder_rest_runtime_t;

bool reminder_rest_runtime_update(reminder_rest_runtime_t *runtime, const app_presence_status_t *presence,
				  int64_t now_us, int64_t *present_us);
void reminder_rest_runtime_mark_fired(reminder_rest_runtime_t *runtime);

#endif
