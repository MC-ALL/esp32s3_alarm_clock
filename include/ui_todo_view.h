#ifndef APP_UI_TODO_VIEW_H_
#define APP_UI_TODO_VIEW_H_

#include <stdint.h>

#include <sync_service.h>

uint8_t ui_todo_item_count(const app_todo_snapshot_t *snapshot);
const app_todo_item_t *ui_todo_item_at(const app_todo_snapshot_t *snapshot, uint8_t index);
uint8_t ui_todo_open_count(const app_todo_snapshot_t *snapshot);

#endif
