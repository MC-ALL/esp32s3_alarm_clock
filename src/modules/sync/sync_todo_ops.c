#include <sync/sync_todo_ops.h>

#include <core/app_bus.h>
#include <core/app_config.h>
#include <sync/sync_event_reporter.h>
#include <sync/sync_todo_cache.h>
#include <sync/sync_transport.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

static int sync_todo_ops_find_index_by_id(const app_todo_snapshot_t *todo_snapshot, const char *todo_id)
{
	if (todo_snapshot == NULL || todo_id == NULL || todo_id[0] == '\0') {
		return -1;
	}
	for (uint8_t i = 0; i < todo_snapshot->count && i < APP_TODO_MAX_ITEMS; i++) {
		if (strcmp(todo_snapshot->items[i].id, todo_id) == 0) {
			return (int)i;
		}
	}
	return -1;
}

static void sync_todo_ops_remove_from_cache(app_todo_snapshot_t *todo_snapshot, const char *todo_id)
{
	const int index = sync_todo_ops_find_index_by_id(todo_snapshot, todo_id);
	if (index < 0) {
		return;
	}
	for (uint8_t i = (uint8_t)index; i + 1U < todo_snapshot->count; i++) {
		todo_snapshot->items[i] = todo_snapshot->items[i + 1U];
	}
	if (todo_snapshot->count > 0U) {
		todo_snapshot->count--;
		todo_snapshot->items[todo_snapshot->count] = (app_todo_item_t){ 0 };
	}
}

static void sync_todo_ops_touch_last_sync(app_todo_snapshot_t *todo_snapshot)
{
	time_t now = 0;
	struct tm timeinfo = { 0 };
	time(&now);
	localtime_r(&now, &timeinfo);
	(void)snprintf(todo_snapshot->last_sync_at, sizeof(todo_snapshot->last_sync_at), "%02d:%02d:%02d",
		       timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

static void sync_todo_ops_publish_error(const char *reason)
{
	app_bus_event_t event = {
		.type = APP_BUS_EVENT_SYNC_FAILED,
	};
	strlcpy(event.data.error.reason, reason != NULL ? reason : "sync failed", sizeof(event.data.error.reason));
	(void)app_bus_publish(&event);
}

static void sync_todo_ops_request_resync(void)
{
	app_bus_event_t event = {
		.type = APP_BUS_EVENT_TODO_SYNC_REQUEST,
	};
	(void)app_bus_publish(&event);
}

static int sync_todo_ops_apply_success(app_todo_snapshot_t *todo_snapshot, const char *todo_id, const char *event_type)
{
	sync_todo_ops_remove_from_cache(todo_snapshot, todo_id);
	todo_snapshot->sync_ok = true;
	todo_snapshot->last_error[0] = '\0';
	sync_todo_ops_touch_last_sync(todo_snapshot);
	sync_todo_cache_save(todo_snapshot);
	sync_event_reporter_report_once(event_type, todo_id);
	return 0;
}

int sync_todo_ops_complete_once(app_todo_snapshot_t *todo_snapshot, const char *todo_id)
{
	if (todo_snapshot == NULL || todo_id == NULL || todo_id[0] == '\0' || !sync_transport_net_ready()) {
		return -1;
	}

	char path[128];
	(void)snprintf(path, sizeof(path), "%s/%s/complete", APP_TODO_WEB_PATH, todo_id);
	char response[256] = { 0 };
	int status = 0;
	int ret = sync_transport_http_request("POST", path, NULL, response, sizeof(response), &status);
	if (ret == 0 && status == 404) {
		todo_snapshot->sync_ok = false;
		strlcpy(todo_snapshot->last_error, "todo missing on web", sizeof(todo_snapshot->last_error));
		sync_todo_ops_publish_error(todo_snapshot->last_error);
		sync_todo_ops_request_resync();
		return 0;
	}
	if (ret != 0 || status != 200) {
		(void)snprintf(todo_snapshot->last_error, sizeof(todo_snapshot->last_error),
				       "todo complete err=%d status=%d", ret, status);
		sync_todo_ops_publish_error(todo_snapshot->last_error);
		return -1;
	}

	return sync_todo_ops_apply_success(todo_snapshot, todo_id, "todo_completed");
}

int sync_todo_ops_delete_once(app_todo_snapshot_t *todo_snapshot, const char *todo_id)
{
	if (todo_snapshot == NULL || todo_id == NULL || todo_id[0] == '\0' || !sync_transport_net_ready()) {
		return -1;
	}

	char path[128];
	(void)snprintf(path, sizeof(path), "%s/%s", APP_TODO_WEB_PATH, todo_id);
	char response[64] = { 0 };
	int status = 0;
	int ret = sync_transport_http_request("DELETE", path, NULL, response, sizeof(response), &status);
	if (ret == 0 && status == 404) {
		todo_snapshot->sync_ok = false;
		strlcpy(todo_snapshot->last_error, "todo missing on web", sizeof(todo_snapshot->last_error));
		sync_todo_ops_publish_error(todo_snapshot->last_error);
		sync_todo_ops_request_resync();
		return 0;
	}
	if (ret != 0 || status != 204) {
		(void)snprintf(todo_snapshot->last_error, sizeof(todo_snapshot->last_error),
				       "todo delete err=%d status=%d", ret, status);
		sync_todo_ops_publish_error(todo_snapshot->last_error);
		return -1;
	}

	return sync_todo_ops_apply_success(todo_snapshot, todo_id, "todo_deleted");
}
