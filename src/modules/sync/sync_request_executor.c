#include <sync/sync_request_executor.h>

#include <stddef.h>

#include <sync/sync_config_pull.h>
#include <sync/sync_event_reporter.h>
#include <sync/sync_settings_push.h>
#include <sync/sync_status_reporter.h>
#include <sync/sync_todo_ops.h>

int sync_request_executor_execute(const sync_request_executor_t *executor, const sync_request_t *request)
{
	if (executor == NULL || request == NULL) {
		return -1;
	}
	if (request->type == SYNC_REQUEST_PULL_CONFIG) {
		sync_config_pull_once(executor->todo_snapshot, executor->device_config_snapshot);
		return 0;
	}
	if (request->type == SYNC_REQUEST_REPORT_STATUS) {
		sync_status_reporter_report_once();
		return 0;
	}
	if (request->type == SYNC_REQUEST_COMPLETE_TODO) {
		return sync_todo_ops_complete_once(executor->todo_snapshot, request->todo_id);
	}
	if (request->type == SYNC_REQUEST_DELETE_TODO) {
		return sync_todo_ops_delete_once(executor->todo_snapshot, request->todo_id);
	}
	if (request->type == SYNC_REQUEST_PUSH_ALARMS) {
		return sync_settings_push_alarms_once(executor->device_config_snapshot, &request->settings,
						      executor->on_config_conflict, executor->conflict_ctx);
	}
	if (request->type == SYNC_REQUEST_PUSH_VOICE) {
		return sync_settings_push_voice_once(executor->device_config_snapshot, &request->settings,
						     executor->on_config_conflict, executor->conflict_ctx);
	}
	if (request->type == SYNC_REQUEST_REPORT_EVENT) {
		sync_event_reporter_report_once(request->event_type, request->todo_id);
		return 0;
	}
	return -1;
}
