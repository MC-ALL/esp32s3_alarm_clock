#include "app_module.h"
#include <app/display_service.h>
#include <app/hw_config.h>
#include <app/module_common.h>

#include <driver/spi_common.h>
#include <driver/spi_master.h>
#include <esp_err.h>
#include <esp_lcd_panel_dev.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <stdint.h>

static const char *TAG = "display";

static esp_lcd_panel_io_handle_t s_panel_io;
static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_lvgl_mutex;
static TaskHandle_t s_lvgl_task;
static esp_timer_handle_t s_lvgl_tick_timer;
static bool s_ready;
static bool s_spi_bus_owned;

#if LVGL_VERSION_MAJOR >= 9
static lv_display_t *s_lvgl_display;
#else
static lv_disp_t *s_lvgl_display;
static lv_disp_draw_buf_t s_lvgl_draw_buf;
static lv_disp_drv_t s_lvgl_drv;
#endif

static uint16_t s_frame_buffer[APP_LCD_WIDTH * 20];
static lv_color_t s_lvgl_buf1[APP_LCD_WIDTH * 20];
static lv_color_t s_lvgl_buf2[APP_LCD_WIDTH * 20];

static const int APP_LCD_X_GAP = 0;
static const int APP_LCD_Y_GAP = 0;

static void lvgl_tick_cb(void *arg)
{
	(void)arg;
	lv_tick_inc(2);
}

#if LVGL_VERSION_MAJOR >= 9
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
	esp_lcd_panel_handle_t panel = lv_display_get_user_data(disp);
	const uint32_t px_count = (uint32_t)((area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1));

	lv_draw_sw_rgb565_swap(px_map, px_count);
	(void)esp_lcd_panel_draw_bitmap(panel, area->x1 + APP_LCD_X_GAP, area->y1 + APP_LCD_Y_GAP,
					area->x2 + 1 + APP_LCD_X_GAP, area->y2 + 1 + APP_LCD_Y_GAP, px_map);
	lv_display_flush_ready(disp);
}

#define APP_LV_SCREEN_ACTIVE() lv_screen_active()
#define APP_LV_TIMER_HANDLER() lv_timer_handler()
#else
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px_map)
{
	esp_lcd_panel_handle_t panel = drv->user_data;
	const uint32_t px_count = (uint32_t)((area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1));

	lv_draw_sw_rgb565_swap(px_map, px_count);
	(void)esp_lcd_panel_draw_bitmap(panel, area->x1 + APP_LCD_X_GAP, area->y1 + APP_LCD_Y_GAP,
					area->x2 + 1 + APP_LCD_X_GAP, area->y2 + 1 + APP_LCD_Y_GAP, px_map);
	lv_disp_flush_ready(drv);
}

#define APP_LV_SCREEN_ACTIVE() lv_scr_act()
#define APP_LV_TIMER_HANDLER() lv_timer_handler()
#endif

static void display_task(void *arg)
{
	(void)arg;

	for (;;) {
		if (display_service_lock(UINT32_MAX)) {
			(void)APP_LV_TIMER_HANDLER();
			display_service_unlock();
		}
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}

static void display_prepare_screen(void)
{
	lv_obj_t *screen = APP_LV_SCREEN_ACTIVE();

	lv_obj_remove_style_all(screen);
	lv_obj_set_size(screen, APP_LCD_WIDTH, APP_LCD_HEIGHT);
	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
	lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

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

	lv_init();
	s_lvgl_mutex = xSemaphoreCreateMutex();
	if (s_lvgl_mutex == NULL) {
		ESP_LOGE(TAG, "failed to create lvgl mutex");
		err = ESP_ERR_NO_MEM;
		goto fail;
	}

#if LVGL_VERSION_MAJOR >= 9
	s_lvgl_display = lv_display_create(APP_LCD_WIDTH, APP_LCD_HEIGHT);
	if (s_lvgl_display == NULL) {
		ESP_LOGE(TAG, "lv_display_create failed");
		err = ESP_ERR_NO_MEM;
		goto fail;
	}
	lv_display_set_buffers(s_lvgl_display, s_lvgl_buf1, s_lvgl_buf2, sizeof(s_lvgl_buf1),
			       LV_DISPLAY_RENDER_MODE_PARTIAL);
	lv_display_set_color_format(s_lvgl_display, LV_COLOR_FORMAT_RGB565);
	lv_display_set_flush_cb(s_lvgl_display, lvgl_flush_cb);
	lv_display_set_user_data(s_lvgl_display, s_panel);
#else
	lv_disp_draw_buf_init(&s_lvgl_draw_buf, s_lvgl_buf1, s_lvgl_buf2, APP_LCD_WIDTH * 20);
	lv_disp_drv_init(&s_lvgl_drv);
	s_lvgl_drv.hor_res = APP_LCD_WIDTH;
	s_lvgl_drv.ver_res = APP_LCD_HEIGHT;
	s_lvgl_drv.flush_cb = lvgl_flush_cb;
	s_lvgl_drv.draw_buf = &s_lvgl_draw_buf;
	s_lvgl_drv.user_data = s_panel;
	s_lvgl_display = lv_disp_drv_register(&s_lvgl_drv);
	if (s_lvgl_display == NULL) {
		ESP_LOGE(TAG, "lv_disp_drv_register failed");
		err = ESP_ERR_NO_MEM;
		goto fail;
	}
#endif

	const esp_timer_create_args_t tick_timer_args = {
		.callback = &lvgl_tick_cb,
		.name = "lvgl_tick",
	};
	err = esp_timer_create(&tick_timer_args, &s_lvgl_tick_timer);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_timer_create failed: %s", esp_err_to_name(err));
		goto fail;
	}

	err = esp_timer_start_periodic(s_lvgl_tick_timer, 2000);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_timer_start_periodic failed: %s", esp_err_to_name(err));
		goto fail;
	}

	if (display_service_lock(100)) {
		display_prepare_screen();
		(void)APP_LV_TIMER_HANDLER();
		display_service_unlock();
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
	BaseType_t ok = xTaskCreate(display_task, "display_task", 4096, NULL, 7, &s_lvgl_task);
	if (ok != pdPASS) {
		ESP_LOGE(TAG, "failed to create display task");
		return -1;
	}

	return 0;
}

int display_service_stop(void)
{
	s_ready = false;

	if (s_lvgl_task != NULL) {
		vTaskDelete(s_lvgl_task);
		s_lvgl_task = NULL;
	}

	if (s_lvgl_tick_timer != NULL) {
		(void)esp_timer_stop(s_lvgl_tick_timer);
		(void)esp_timer_delete(s_lvgl_tick_timer);
		s_lvgl_tick_timer = NULL;
	}

	if (s_lvgl_mutex != NULL) {
		vSemaphoreDelete(s_lvgl_mutex);
		s_lvgl_mutex = NULL;
	}

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
	if (s_lvgl_mutex == NULL) {
		return false;
	}

	const TickType_t ticks = timeout_ms == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
	return xSemaphoreTake(s_lvgl_mutex, ticks) == pdTRUE;
}

void display_service_unlock(void)
{
	if (s_lvgl_mutex != NULL) {
		xSemaphoreGive(s_lvgl_mutex);
	}
}

lv_obj_t *display_service_get_screen(void)
{
	if (!s_ready) {
		return NULL;
	}

	return APP_LV_SCREEN_ACTIVE();
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
