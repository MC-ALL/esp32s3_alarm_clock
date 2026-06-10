#include "app_module.h"
#include <core/module_common.h>

#include <esp_log.h>

static const char *TAG = "lifecycle";

int lifecycle_service_init(void)
{
	ESP_LOGI(TAG, "init");
	return 0;
}

int lifecycle_service_start(void)
{
	ESP_LOGI(TAG, "start");
	return 0;
}

int lifecycle_service_stop(void)
{
	ESP_LOGI(TAG, "stop");
	return 0;
}
