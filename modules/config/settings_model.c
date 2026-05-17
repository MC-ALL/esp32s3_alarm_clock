#include "app_module.h"
#include <app/module_common.h>

#include <esp_log.h>

static const char *TAG = "settings";

int settings_model_init(void)
{
	ESP_LOGI(TAG, "init");
	return 0;
}

int settings_model_start(void)
{
	return 0;
}

int settings_model_stop(void)
{
	return 0;
}
