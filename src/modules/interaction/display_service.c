#include "app_module.h"
#include <display_lvgl_port.h>
#include <display_panel.h>
#include <display_service.h>
#include <hw_config.h>
#include <module_common.h>

#include <esp_err.h>
#include <esp_log.h>

static const char *TAG = "display";

static bool s_ready;

int display_service_init(void)
{
	int ret = display_panel_init();
	if (ret != 0) {
		return ret;
	}

	esp_err_t err = (esp_err_t)display_lvgl_port_init(display_panel_handle());
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "display_lvgl_port_init failed: %s", esp_err_to_name(err));
		goto fail;
	}

	s_ready = true;
	ESP_LOGI(TAG, "init lcd %dx%d", APP_LCD_WIDTH, APP_LCD_HEIGHT);
	return 0;

fail:
	(void)display_service_stop();
	return (int)err;
}

int display_service_start(void)
{
	return display_lvgl_port_start();
}

int display_service_stop(void)
{
	s_ready = false;

	display_lvgl_port_stop();

	display_panel_deinit();

	return 0;
}

bool display_service_is_ready(void)
{
	return s_ready;
}

bool display_service_lock(uint32_t timeout_ms)
{
	return display_lvgl_port_lock(timeout_ms);
}

void display_service_unlock(void)
{
	display_lvgl_port_unlock();
}

lv_obj_t *display_service_get_screen(void)
{
	if (!s_ready) {
		return NULL;
	}

	return display_lvgl_port_get_screen();
}

int display_service_fill_color(uint16_t rgb565)
{
	if (!s_ready) {
		return -1;
	}
	return display_panel_fill_color(rgb565);
}
