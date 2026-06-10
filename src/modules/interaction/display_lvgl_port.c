#include <display_lvgl_port.h>
#include <display_lvgl_runtime.h>
#include <hw_config.h>

#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdint.h>

static const char *TAG = "display_lvgl";

static SemaphoreHandle_t s_lvgl_mutex;

#if LVGL_VERSION_MAJOR >= 9
static lv_display_t *s_lvgl_display;
#else
static lv_disp_t *s_lvgl_display;
static lv_disp_draw_buf_t s_lvgl_draw_buf;
static lv_disp_drv_t s_lvgl_drv;
#endif

static lv_color_t s_lvgl_buf1[APP_LCD_WIDTH * 20];
static lv_color_t s_lvgl_buf2[APP_LCD_WIDTH * 20];

static const int APP_LCD_X_GAP = 0;
static const int APP_LCD_Y_GAP = 0;

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
#endif

static void display_prepare_screen(void)
{
	lv_obj_t *screen = APP_LV_SCREEN_ACTIVE();

	lv_obj_remove_style_all(screen);
	lv_obj_set_size(screen, APP_LCD_WIDTH, APP_LCD_HEIGHT);
	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
	lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

int display_lvgl_port_init(esp_lcd_panel_handle_t panel)
{
	lv_init();
	s_lvgl_mutex = xSemaphoreCreateMutex();
	if (s_lvgl_mutex == NULL) {
		ESP_LOGE(TAG, "failed to create lvgl mutex");
		return (int)ESP_ERR_NO_MEM;
	}

#if LVGL_VERSION_MAJOR >= 9
	s_lvgl_display = lv_display_create(APP_LCD_WIDTH, APP_LCD_HEIGHT);
	if (s_lvgl_display == NULL) {
		ESP_LOGE(TAG, "lv_display_create failed");
		display_lvgl_port_stop();
		return (int)ESP_ERR_NO_MEM;
	}
	lv_display_set_buffers(s_lvgl_display, s_lvgl_buf1, s_lvgl_buf2, sizeof(s_lvgl_buf1),
			       LV_DISPLAY_RENDER_MODE_PARTIAL);
	lv_display_set_color_format(s_lvgl_display, LV_COLOR_FORMAT_RGB565);
	lv_display_set_flush_cb(s_lvgl_display, lvgl_flush_cb);
	lv_display_set_user_data(s_lvgl_display, panel);
#else
	lv_disp_draw_buf_init(&s_lvgl_draw_buf, s_lvgl_buf1, s_lvgl_buf2, APP_LCD_WIDTH * 20);
	lv_disp_drv_init(&s_lvgl_drv);
	s_lvgl_drv.hor_res = APP_LCD_WIDTH;
	s_lvgl_drv.ver_res = APP_LCD_HEIGHT;
	s_lvgl_drv.flush_cb = lvgl_flush_cb;
	s_lvgl_drv.draw_buf = &s_lvgl_draw_buf;
	s_lvgl_drv.user_data = panel;
	s_lvgl_display = lv_disp_drv_register(&s_lvgl_drv);
	if (s_lvgl_display == NULL) {
		ESP_LOGE(TAG, "lv_disp_drv_register failed");
		display_lvgl_port_stop();
		return (int)ESP_ERR_NO_MEM;
	}
#endif

	int ret = display_lvgl_runtime_init(display_lvgl_port_lock, display_lvgl_port_unlock);
	if (ret != 0) {
		display_lvgl_port_stop();
		return ret;
	}

	if (display_lvgl_port_lock(100)) {
		display_prepare_screen();
		display_lvgl_runtime_handle_timer();
		display_lvgl_port_unlock();
	}

	return 0;
}

int display_lvgl_port_start(void)
{
	return display_lvgl_runtime_start();
}

void display_lvgl_port_stop(void)
{
	display_lvgl_runtime_stop();

	if (s_lvgl_mutex != NULL) {
		vSemaphoreDelete(s_lvgl_mutex);
		s_lvgl_mutex = NULL;
	}

	s_lvgl_display = NULL;
}

bool display_lvgl_port_lock(uint32_t timeout_ms)
{
	if (s_lvgl_mutex == NULL) {
		return false;
	}

	const TickType_t ticks = timeout_ms == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
	return xSemaphoreTake(s_lvgl_mutex, ticks) == pdTRUE;
}

void display_lvgl_port_unlock(void)
{
	if (s_lvgl_mutex != NULL) {
		xSemaphoreGive(s_lvgl_mutex);
	}
}

lv_obj_t *display_lvgl_port_get_screen(void)
{
	return APP_LV_SCREEN_ACTIVE();
}
