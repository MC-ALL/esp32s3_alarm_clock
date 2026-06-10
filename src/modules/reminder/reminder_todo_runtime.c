#include <reminder_todo_runtime.h>

#include <esp_log.h>
#include <string.h>

static const char *TAG = "reminder_todo";

static bool todo_id_known(const reminder_todo_runtime_t *runtime, const char *id)
{
	for (uint8_t i = 0; i < runtime->known_count; i++) {
		if (strcmp(runtime->known_ids[i], id) == 0) {
			return true;
		}
	}
	return false;
}

static void remember_todo_ids(reminder_todo_runtime_t *runtime, const app_todo_snapshot_t *todo)
{
	runtime->known_count = 0;
	for (uint8_t i = 0; i < todo->count && i < APP_TODO_MAX_ITEMS; i++) {
		if (todo->items[i].id[0] == '\0') {
			continue;
		}
		strlcpy(runtime->known_ids[runtime->known_count], todo->items[i].id, sizeof(runtime->known_ids[0]));
		runtime->known_count++;
	}
}

uint8_t reminder_todo_runtime_update(reminder_todo_runtime_t *runtime, const app_todo_snapshot_t *todo)
{
	if (runtime == NULL || todo == NULL || !todo->sync_ok || todo->sync_in_progress) {
		return 0;
	}

	uint8_t new_count = 0;
	for (uint8_t i = 0; i < todo->count && i < APP_TODO_MAX_ITEMS; i++) {
		const app_todo_item_t *item = &todo->items[i];
		if (item->done || item->id[0] == '\0') {
			continue;
		}
		if (runtime->seen_once && !todo_id_known(runtime, item->id)) {
			new_count++;
			ESP_LOGI(TAG, "new todo detected id=%s text=%.32s", item->id, item->text);
		}
	}

	remember_todo_ids(runtime, todo);
	if (!runtime->seen_once) {
		runtime->seen_once = true;
		return 0;
	}

	return new_count;
}
