#include "app_module.h"
#include <module_common.h>

#include <esp_log.h>

static const char *TAG = "timebase";

int timebase_service_init(void)
{
	ESP_LOGI(TAG, "init");
	return 0;
}

int timebase_service_start(void)
{
	return 0;
}

int timebase_service_stop(void)
{
	return 0;
}
