#include "app_module.h"
#include <app/module_common.h>

#include <esp_log.h>

static const char *TAG = "ui_model";

int ui_model_init(void)
{
	ESP_LOGI(TAG, "init");
	return 0;
}

int ui_model_start(void)
{
	return 0;
}

int ui_model_stop(void)
{
	return 0;
}
