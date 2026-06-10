#include "app_module.h"
#include <display_lvgl_port.h>
#include <display_service.h>
#include <hw_config.h>
#include <module_common.h>

#include <driver/spi_common.h>
#include <driver/spi_master.h>
#include <esp_err.h>
#include <esp_lcd_panel_dev.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <stdint.h>

static const char *TAG = "display";

static esp_lcd_panel_io_handle_t s_panel_io;
static esp_lcd_panel_handle_t s_panel;
static bool s_ready;
static bool s_spi_bus_owned;

static uint16_t s_frame_buffer[APP_LCD_WIDTH * 20];

static const int APP_LCD_X_GAP = 0;
static const int APP_LCD_Y_GAP = 0;

int display_service_init(void)
{
	const spi_bus_config_t buscfg = {
		.sclk_io_num = APP_PIN_LCD_SCLK,
		.mosi_io_num = APP_PIN_LCD_MOSI,
		.miso_io_num = -1,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
		.max_transfer_sz = APP_LCD_WIDTH * 20 * sizeof(uint16_t),
	};
	const esp_lcd_panel_io_spi_config_t io_config = {
		.cs_gpio_num = APP_PIN_LCD_CS,
		.dc_gpio_num = APP_PIN_LCD_DC,
		.spi_mode = 0,
		.pclk_hz = APP_LCD_PIXEL_CLOCK_HZ,
		.trans_queue_depth = 10,
		.lcd_cmd_bits = 8,
		.lcd_param_bits = 8,
	};

	esp_err_t err = spi_bus_initialize(APP_LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
	if (err == ESP_OK) {
		s_spi_bus_owned = true;
	} else if (err != ESP_ERR_INVALID_STATE) {
		ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)APP_LCD_SPI_HOST, &io_config, &s_panel_io);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_lcd_new_panel_io_spi failed: %s", esp_err_to_name(err));
		goto fail;
	}

	const esp_lcd_panel_dev_config_t panel_config = {
		.reset_gpio_num = APP_PIN_LCD_RST,
		.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
		.bits_per_pixel = 16,
	};

	err = esp_lcd_new_panel_st7789(s_panel_io, &panel_config, &s_panel);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_lcd_new_panel_st7789 failed: %s", esp_err_to_name(err));
		goto fail;
	}

	err = esp_lcd_panel_reset(s_panel);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_lcd_panel_reset failed: %s", esp_err_to_name(err));
		goto fail;
	}

	err = esp_lcd_panel_init(s_panel);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_lcd_panel_init failed: %s", esp_err_to_name(err));
		goto fail;
	}

	err = esp_lcd_panel_invert_color(s_panel, true);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_lcd_panel_invert_color failed: %s", esp_err_to_name(err));
		goto fail;
	}

	err = esp_lcd_panel_swap_xy(s_panel, false);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_lcd_panel_swap_xy failed: %s", esp_err_to_name(err));
		goto fail;
	}

	err = esp_lcd_panel_mirror(s_panel, true, true);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_lcd_panel_mirror failed: %s", esp_err_to_name(err));
		goto fail;
	}

	err = esp_lcd_panel_set_gap(s_panel, APP_LCD_X_GAP, APP_LCD_Y_GAP);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_lcd_panel_set_gap failed: %s", esp_err_to_name(err));
		goto fail;
	}

	err = esp_lcd_panel_disp_on_off(s_panel, true);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_lcd_panel_disp_on_off failed: %s", esp_err_to_name(err));
		goto fail;
	}

	err = (esp_err_t)display_lvgl_port_init(s_panel);
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

	if (s_panel != NULL) {
		(void)esp_lcd_panel_disp_on_off(s_panel, false);
		(void)esp_lcd_panel_del(s_panel);
		s_panel = NULL;
	}
	if (s_panel_io != NULL) {
		(void)esp_lcd_panel_io_del(s_panel_io);
		s_panel_io = NULL;
	}
	if (s_spi_bus_owned) {
		(void)spi_bus_free(APP_LCD_SPI_HOST);
		s_spi_bus_owned = false;
	}

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
	if (!s_ready || s_panel == NULL) {
		return -1;
	}

	for (size_t i = 0; i < (sizeof(s_frame_buffer) / sizeof(s_frame_buffer[0])); i++) {
		s_frame_buffer[i] = rgb565;
	}

	for (int y = 0; y < APP_LCD_HEIGHT; y += 20) {
		int y2 = y + 20;
		if (y2 > APP_LCD_HEIGHT) {
			y2 = APP_LCD_HEIGHT;
		}

		esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, y, APP_LCD_WIDTH, y2, s_frame_buffer);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "draw_bitmap failed at y=%d: %s", y, esp_err_to_name(err));
			return (int)err;
		}
	}

	return 0;
}
