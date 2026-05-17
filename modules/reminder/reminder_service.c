#include "app_module.h"
#include <app/module_common.h>

#include <esp_log.h>

static const char *TAG = "reminder";

int reminder_service_init(void)
{
	ESP_LOGI(TAG, "init");
	return 0;
}

int reminder_service_start(void)
{
	return 0;
}

int reminder_service_stop(void)
{
	return 0;
}
