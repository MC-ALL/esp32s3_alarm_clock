#ifndef SYNC_CONFIG_PULL_H_
#define SYNC_CONFIG_PULL_H_

#include <sync_service.h>

void sync_config_pull_once(app_todo_snapshot_t *todo_snapshot, app_device_config_snapshot_t *device_config_snapshot);
void sync_config_pull_reset_backoff(void);

#endif
