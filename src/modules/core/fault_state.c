#include "app_module.h"
#include <core/module_common.h>

#include <esp_log.h>

static const char *TAG = "fault_state";

int fault_state_init(void)
{
	ESP_LOGI(TAG, "init");
	return 0;
}

int fault_state_start(void)
{
	return 0;
}

int fault_state_stop(void)
{
	return 0;
}
