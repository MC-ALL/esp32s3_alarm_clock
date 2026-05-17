#include "app_boot.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "main";

void app_main(void)
{
	int ret = app_boot_start();
	if (ret != 0) {
		ESP_LOGE(TAG, "boot failed: %d", ret);
		return;
	}

	while (true) {
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}
