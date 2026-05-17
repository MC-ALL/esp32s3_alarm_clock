#include "app_module.h"
#include <app/module_common.h>

#include <esp_err.h>
#include <nvs_flash.h>
#include <esp_log.h>

static const char *TAG = "persist";

int persistence_broker_init(void)
{
	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		err = nvs_flash_erase();
		if (err == ESP_OK) {
			err = nvs_flash_init();
		}
	}

	if (err != ESP_OK) {
		ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	ESP_LOGI(TAG, "init");
	return 0;
}

int persistence_broker_start(void)
{
	return 0;
}

int persistence_broker_stop(void)
{
	return 0;
}
