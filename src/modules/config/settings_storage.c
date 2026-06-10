#include <config/settings_storage.h>

#include <esp_err.h>
#include <esp_log.h>
#include <nvs.h>

static const char *TAG = "settings_storage";

#define APP_SETTINGS_NAMESPACE "settings"
#define APP_SETTINGS_BLOB_KEY "ui_v1"
#define APP_SETTINGS_MAGIC 0x53434c4bU
#define APP_SETTINGS_VERSION 2U

typedef struct {
	uint32_t magic;
	uint16_t version;
	uint16_t size;
	app_settings_t settings;
} settings_blob_t;

settings_storage_load_result_t settings_storage_load(app_settings_t *settings)
{
	if (settings == NULL) {
		return SETTINGS_STORAGE_LOAD_ERROR;
	}

	nvs_handle_t handle = 0;
	esp_err_t err = nvs_open(APP_SETTINGS_NAMESPACE, NVS_READONLY, &handle);
	if (err == ESP_ERR_NVS_NOT_FOUND) {
		ESP_LOGI(TAG, "no saved settings, using defaults");
		return SETTINGS_STORAGE_LOAD_MISSING;
	}
	if (err != ESP_OK) {
		ESP_LOGW(TAG, "nvs_open read failed: %s", esp_err_to_name(err));
		return SETTINGS_STORAGE_LOAD_ERROR;
	}

	settings_blob_t blob = { 0 };
	size_t size = sizeof(blob);
	err = nvs_get_blob(handle, APP_SETTINGS_BLOB_KEY, &blob, &size);
	nvs_close(handle);
	if (err != ESP_OK) {
		ESP_LOGI(TAG, "settings blob missing: %s", esp_err_to_name(err));
		return SETTINGS_STORAGE_LOAD_MISSING;
	}
	if (size != sizeof(blob) || blob.magic != APP_SETTINGS_MAGIC || blob.version != APP_SETTINGS_VERSION ||
	    blob.size != sizeof(blob.settings)) {
		ESP_LOGW(TAG, "settings blob incompatible size=%u magic=0x%08x version=%u",
			 (unsigned)size, (unsigned)blob.magic, (unsigned)blob.version);
		return SETTINGS_STORAGE_LOAD_INCOMPATIBLE;
	}

	*settings = blob.settings;
	return SETTINGS_STORAGE_LOAD_OK;
}

int settings_storage_save(const app_settings_t *settings)
{
	if (settings == NULL) {
		return -1;
	}

	const settings_blob_t blob = {
		.magic = APP_SETTINGS_MAGIC,
		.version = APP_SETTINGS_VERSION,
		.size = sizeof(*settings),
		.settings = *settings,
	};

	nvs_handle_t handle = 0;
	esp_err_t err = nvs_open(APP_SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "nvs_open write failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	err = nvs_set_blob(handle, APP_SETTINGS_BLOB_KEY, &blob, sizeof(blob));
	if (err == ESP_OK) {
		err = nvs_commit(handle);
	}
	nvs_close(handle);

	if (err != ESP_OK) {
		ESP_LOGE(TAG, "settings save failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	return 0;
}
