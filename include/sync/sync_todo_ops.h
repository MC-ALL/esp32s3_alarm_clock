#ifndef SYNC_TODO_OPS_H_
#define SYNC_TODO_OPS_H_

#include <sync/sync_service.h>

int sync_todo_ops_complete_once(app_todo_snapshot_t *todo_snapshot, const char *todo_id);
int sync_todo_ops_delete_once(app_todo_snapshot_t *todo_snapshot, const char *todo_id);

#endif
