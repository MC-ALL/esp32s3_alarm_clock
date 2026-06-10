#include <sync_todo_cache.h>

#include <esp_err.h>
#include <esp_log.h>
#include <nvs.h>

static const char *TAG = "sync_cache";

#define APP_TODO_CACHE_NAMESPACE "todo_cache"
#define APP_TODO_CACHE_BLOB_KEY "snapshot_v1"
#define APP_TODO_CACHE_MAGIC 0x544F444FU
#define APP_TODO_CACHE_VERSION 1U

typedef struct {
	uint32_t magic;
	uint16_t version;
	uint16_t size;
	app_todo_snapshot_t snapshot;
} sync_todo_cache_blob_t;

void sync_todo_cache_sanitize(app_todo_snapshot_t *snapshot)
{
	if (snapshot == NULL) {
		return;
	}
	if (snapshot->count > APP_TODO_MAX_ITEMS) {
		snapshot->count = APP_TODO_MAX_ITEMS;
	}
	snapshot->sync_in_progress = false;
	if (!snapshot->sync_ok) {
		snapshot->last_error[0] = '\0';
	}
}

void sync_todo_cache_save(const app_todo_snapshot_t *snapshot)
{
	if (snapshot == NULL) {
		return;
	}

	sync_todo_cache_blob_t blob = {
		.magic = APP_TODO_CACHE_MAGIC,
		.version = APP_TODO_CACHE_VERSION,
		.size = sizeof(blob.snapshot),
		.snapshot = *snapshot,
	};
	sync_todo_cache_sanitize(&blob.snapshot);

	nvs_handle_t handle = 0;
	esp_err_t err = nvs_open(APP_TODO_CACHE_NAMESPACE, NVS_READWRITE, &handle);
	if (err != ESP_OK) {
		ESP_LOGW(TAG, "todo cache open write failed: %s", esp_err_to_name(err));
		return;
	}
	err = nvs_set_blob(handle, APP_TODO_CACHE_BLOB_KEY, &blob, sizeof(blob));
	if (err == ESP_OK) {
		err = nvs_commit(handle);
	}
	nvs_close(handle);
	if (err != ESP_OK) {
		ESP_LOGW(TAG, "todo cache save failed: %s", esp_err_to_name(err));
	}
}

bool sync_todo_cache_load(app_todo_snapshot_t *out_snapshot)
{
	if (out_snapshot == NULL) {
		return false;
	}

	nvs_handle_t handle = 0;
	esp_err_t err = nvs_open(APP_TODO_CACHE_NAMESPACE, NVS_READONLY, &handle);
	if (err != ESP_OK) {
		return false;
	}

	sync_todo_cache_blob_t blob = { 0 };
	size_t size = sizeof(blob);
	err = nvs_get_blob(handle, APP_TODO_CACHE_BLOB_KEY, &blob, &size);
	nvs_close(handle);
	if (err != ESP_OK || size != sizeof(blob) || blob.magic != APP_TODO_CACHE_MAGIC ||
	    blob.version != APP_TODO_CACHE_VERSION || blob.size != sizeof(blob.snapshot)) {
		return false;
	}

	sync_todo_cache_sanitize(&blob.snapshot);
	*out_snapshot = blob.snapshot;
	return true;
}
