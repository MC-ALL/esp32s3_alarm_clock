#include <interaction/ui_todo_view.h>

uint8_t ui_todo_item_count(const app_todo_snapshot_t *snapshot)
{
	return snapshot != NULL && snapshot->count < APP_TODO_MAX_ITEMS ? snapshot->count : APP_TODO_MAX_ITEMS;
}

const app_todo_item_t *ui_todo_item_at(const app_todo_snapshot_t *snapshot, uint8_t index)
{
	if (snapshot == NULL || index >= ui_todo_item_count(snapshot)) {
		return NULL;
	}
	return &snapshot->items[index];
}

uint8_t ui_todo_open_count(const app_todo_snapshot_t *snapshot)
{
	uint8_t count = 0;
	if (snapshot == NULL) {
		return 0;
	}

	for (uint8_t i = 0; i < snapshot->count && i < APP_TODO_MAX_ITEMS; i++) {
		if (!snapshot->items[i].done) {
			count++;
		}
	}
	return count;
}
