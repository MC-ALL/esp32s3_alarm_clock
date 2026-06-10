#ifndef SYNC_REQUEST_EXECUTOR_H_
#define SYNC_REQUEST_EXECUTOR_H_

#include <sync_request_retry.h>
#include <sync_service.h>

typedef void (*sync_request_executor_conflict_fn_t)(void *ctx);

typedef struct {
	app_todo_snapshot_t *todo_snapshot;
	app_device_config_snapshot_t *device_config_snapshot;
	sync_request_executor_conflict_fn_t on_config_conflict;
	void *conflict_ctx;
} sync_request_executor_t;

int sync_request_executor_execute(const sync_request_executor_t *executor, const sync_request_t *request);

#endif
