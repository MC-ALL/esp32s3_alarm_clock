#include "app_module.h"
#include <module_common.h>
#include <settings_model.h>

#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <nvs.h>
#include <string.h>

static const char *TAG = "settings";

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

static app_settings_t s_runtime_settings;
static SemaphoreHandle_t s_settings_mutex;

static void settings_model_cache_update(const app_settings_t *settings)
{
	if (settings == NULL) {
		return;
	}

	if (s_settings_mutex != NULL) {
		if (xSemaphoreTake(s_settings_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
			s_runtime_settings = *settings;
			xSemaphoreGive(s_settings_mutex);
		}
		return;
	}

	s_runtime_settings = *settings;
}

void settings_model_defaults(app_settings_t *settings)
{
	if (settings == NULL) {
		return;
	}

	*settings = (app_settings_t){
		.use_24h = true,
		.low_use_24h = true,
		.home_hour_chime_on = true,
		.alarm_voice_on = true,
		.todo_voice_on = true,
		.env_voice_on = true,
		.env_alert_on = true,
		.todo_refresh_min = 5,
		.env_sample_s = 10,
		.env_temp_low_c = 10,
		.env_temp_high_c = 35,
		.env_humi_low_percent = 30,
		.env_humi_high_percent = 80,
		.env_lux_low = 20,
		.env_lux_high = 1000,
		.alarm_count = 3,
		.low_enter_absent_s = 60,
		.low_exit_present_s = 3,
		.alarms = {
			{ .hour = 7, .minute = 30, .repeat = true, .enabled = true, .voice = true },
			{ .hour = 8, .minute = 0, .repeat = true, .enabled = true, .voice = true },
			{ .hour = 20, .minute = 15, .repeat = false, .enabled = true, .voice = false },
		},
	};
}

static void settings_model_sanitize(app_settings_t *settings)
{
	if (settings == NULL) {
		return;
	}

	if (settings->alarm_count > APP_SETTINGS_MAX_ALARMS) {
		settings->alarm_count = APP_SETTINGS_MAX_ALARMS;
	}
	if (settings->todo_refresh_min == 0U) {
		settings->todo_refresh_min = 5U;
	} else if (settings->todo_refresh_min > 30U) {
		settings->todo_refresh_min = 30U;
	}
	if (settings->env_sample_s < 5U) {
		settings->env_sample_s = 5U;
	} else if (settings->env_sample_s > 30U) {
		settings->env_sample_s = 30U;
	}
	if (settings->env_temp_low_c >= settings->env_temp_high_c) {
		settings->env_temp_low_c = 10;
		settings->env_temp_high_c = 35;
	}
	if (settings->env_humi_low_percent >= settings->env_humi_high_percent ||
	    settings->env_humi_high_percent > 100U) {
		settings->env_humi_low_percent = 30U;
		settings->env_humi_high_percent = 80U;
	}
	if (settings->env_lux_low >= settings->env_lux_high) {
		settings->env_lux_low = 20U;
		settings->env_lux_high = 1000U;
	}
	if (settings->low_enter_absent_s < 5U) {
		settings->low_enter_absent_s = 5U;
	}
	if (settings->low_exit_present_s == 0U) {
		settings->low_exit_present_s = 1U;
	}

	for (uint8_t i = 0; i < settings->alarm_count; i++) {
		settings->alarms[i].hour %= 24U;
		settings->alarms[i].minute %= 60U;
	}
}

bool settings_model_load(app_settings_t *settings)
{
	if (settings == NULL) {
		return false;
	}

	settings_model_defaults(settings);

	nvs_handle_t handle = 0;
	esp_err_t err = nvs_open(APP_SETTINGS_NAMESPACE, NVS_READONLY, &handle);
	if (err == ESP_ERR_NVS_NOT_FOUND) {
		ESP_LOGI(TAG, "no saved settings, using defaults");
		settings_model_cache_update(settings);
		return false;
	}
	if (err != ESP_OK) {
		ESP_LOGW(TAG, "nvs_open read failed: %s", esp_err_to_name(err));
		settings_model_cache_update(settings);
		return false;
	}

	settings_blob_t blob = { 0 };
	size_t size = sizeof(blob);
	err = nvs_get_blob(handle, APP_SETTINGS_BLOB_KEY, &blob, &size);
	nvs_close(handle);
	if (err != ESP_OK) {
		ESP_LOGI(TAG, "settings blob missing: %s", esp_err_to_name(err));
		settings_model_cache_update(settings);
		return false;
	}
	if (size != sizeof(blob) || blob.magic != APP_SETTINGS_MAGIC || blob.version != APP_SETTINGS_VERSION ||
	    blob.size != sizeof(blob.settings)) {
		ESP_LOGW(TAG, "settings blob incompatible size=%u magic=0x%08x version=%u",
			 (unsigned)size, (unsigned)blob.magic, (unsigned)blob.version);
		settings_model_cache_update(settings);
		return false;
	}

	*settings = blob.settings;
	settings_model_sanitize(settings);
	settings_model_cache_update(settings);
	ESP_LOGI(TAG,
		 "settings loaded alarms=%u low_enter=%us low_exit=%us "
		 "env_temp=[%d,%d] env_humi=[%u,%u] env_lux=[%u,%u]",
		 (unsigned)settings->alarm_count,
		 (unsigned)settings->low_enter_absent_s,
		 (unsigned)settings->low_exit_present_s,
		 (int)settings->env_temp_low_c,
		 (int)settings->env_temp_high_c,
		 (unsigned)settings->env_humi_low_percent,
		 (unsigned)settings->env_humi_high_percent,
		 (unsigned)settings->env_lux_low,
		 (unsigned)settings->env_lux_high);
	return true;
}

static int settings_model_save_blob(const app_settings_t *settings)
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

int settings_model_save(const app_settings_t *settings)
{
	if (settings == NULL) {
		return -1;
	}

	app_settings_t sanitized = *settings;
	settings_model_sanitize(&sanitized);
	int ret = settings_model_save_blob(&sanitized);
	if (ret != 0) {
		return ret;
	}

	settings_model_cache_update(&sanitized);
	ESP_LOGI(TAG,
		 "settings saved alarms=%u low_enter=%us low_exit=%us "
		 "env_temp=[%d,%d] env_humi=[%u,%u] env_lux=[%u,%u]",
		 (unsigned)sanitized.alarm_count,
		 (unsigned)sanitized.low_enter_absent_s,
		 (unsigned)sanitized.low_exit_present_s,
		 (int)sanitized.env_temp_low_c,
		 (int)sanitized.env_temp_high_c,
		 (unsigned)sanitized.env_humi_low_percent,
		 (unsigned)sanitized.env_humi_high_percent,
		 (unsigned)sanitized.env_lux_low,
		 (unsigned)sanitized.env_lux_high);
	return 0;
}

void settings_model_get(app_settings_t *settings)
{
	if (settings == NULL) {
		return;
	}

	if (s_settings_mutex != NULL) {
		if (xSemaphoreTake(s_settings_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
			*settings = s_runtime_settings;
			xSemaphoreGive(s_settings_mutex);
			return;
		}
	}

	*settings = s_runtime_settings;
}

int settings_model_set(const app_settings_t *settings)
{
	return settings_model_save(settings);
}

int settings_model_init(void)
{
	settings_model_defaults(&s_runtime_settings);
	s_settings_mutex = xSemaphoreCreateMutex();
	if (s_settings_mutex == NULL) {
		ESP_LOGE(TAG, "failed to create settings mutex");
		return -1;
	}

	app_settings_t settings = { 0 };
	(void)settings_model_load(&settings);
	ESP_LOGI(TAG, "init");
	return 0;
}

int settings_model_start(void)
{
	return 0;
}

int settings_model_stop(void)
{
	if (s_settings_mutex != NULL) {
		vSemaphoreDelete(s_settings_mutex);
		s_settings_mutex = NULL;
	}
	return 0;
}
