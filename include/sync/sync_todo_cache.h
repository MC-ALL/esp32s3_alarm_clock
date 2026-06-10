#ifndef APP_SYNC_TODO_CACHE_H_
#define APP_SYNC_TODO_CACHE_H_

#include <stdbool.h>

#include <sync/sync_service.h>

void sync_todo_cache_sanitize(app_todo_snapshot_t *snapshot);
void sync_todo_cache_save(const app_todo_snapshot_t *snapshot);
bool sync_todo_cache_load(app_todo_snapshot_t *out_snapshot);

#endif
