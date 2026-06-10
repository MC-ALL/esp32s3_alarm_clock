#include "app_module.h"
#include <module_common.h>
#include <settings_defaults.h>
#include <settings_model.h>
#include <settings_storage.h>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static const char *TAG = "settings";

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
	settings_defaults_apply(settings);
}

bool settings_model_load(app_settings_t *settings)
{
	if (settings == NULL) {
		return false;
	}

	settings_model_defaults(settings);
	settings_storage_load_result_t result = settings_storage_load(settings);
	if (result != SETTINGS_STORAGE_LOAD_OK) {
		settings_model_cache_update(settings);
		return false;
	}

	settings_defaults_sanitize(settings);
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

int settings_model_save(const app_settings_t *settings)
{
	if (settings == NULL) {
		return -1;
	}

	app_settings_t sanitized = *settings;
	settings_defaults_sanitize(&sanitized);
	int ret = settings_storage_save(&sanitized);
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
