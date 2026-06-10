#ifndef REMINDER_TODO_RUNTIME_H_
#define REMINDER_TODO_RUNTIME_H_

#include <stdbool.h>
#include <stdint.h>

#include <sync_service.h>

typedef struct {
	bool seen_once;
	char known_ids[APP_TODO_MAX_ITEMS][24];
	uint8_t known_count;
} reminder_todo_runtime_t;

uint8_t reminder_todo_runtime_update(reminder_todo_runtime_t *runtime, const app_todo_snapshot_t *todo);

#endif
