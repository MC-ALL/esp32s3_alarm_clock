#include "app_boot.h"
#include "app_module.h"

#include <esp_log.h>

static const char *TAG = "app_boot";

int app_boot_start(void)
{
	int ret = app_modules_init_all();
	if (ret != 0) {
		ESP_LOGE(TAG, "module init failed: %d", ret);
		return ret;
	}

	ret = app_modules_start_all();
	if (ret != 0) {
		ESP_LOGE(TAG, "module start failed: %d", ret);
		return ret;
	}

	ESP_LOGI(TAG, "smart clock boot complete");
	return 0;
}

void app_boot_shutdown(void)
{
	(void)app_modules_stop_all();
	ESP_LOGI(TAG, "smart clock shutdown complete");
}
