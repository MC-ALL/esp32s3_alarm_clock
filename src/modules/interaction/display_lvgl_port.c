#include <display_lvgl_port.h>
#include <hw_config.h>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <stdint.h>

static const char *TAG = "display_lvgl";

static SemaphoreHandle_t s_lvgl_mutex;
static TaskHandle_t s_lvgl_task;
static esp_timer_handle_t s_lvgl_tick_timer;

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
		if (display_lvgl_port_lock(UINT32_MAX)) {
			(void)APP_LV_TIMER_HANDLER();
			display_lvgl_port_unlock();
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

int display_lvgl_port_init(esp_lcd_panel_handle_t panel)
{
	esp_err_t err;

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

	const esp_timer_create_args_t tick_timer_args = {
		.callback = &lvgl_tick_cb,
		.name = "lvgl_tick",
	};
	err = esp_timer_create(&tick_timer_args, &s_lvgl_tick_timer);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_timer_create failed: %s", esp_err_to_name(err));
		display_lvgl_port_stop();
		return (int)err;
	}

	err = esp_timer_start_periodic(s_lvgl_tick_timer, 2000);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_timer_start_periodic failed: %s", esp_err_to_name(err));
		display_lvgl_port_stop();
		return (int)err;
	}

	if (display_lvgl_port_lock(100)) {
		display_prepare_screen();
		(void)APP_LV_TIMER_HANDLER();
		display_lvgl_port_unlock();
	}

	return 0;
}

int display_lvgl_port_start(void)
{
	BaseType_t ok = xTaskCreate(display_task, "display_task", 4096, NULL, 7, &s_lvgl_task);
	if (ok != pdPASS) {
		ESP_LOGE(TAG, "failed to create display task");
		return -1;
	}

	return 0;
}

void display_lvgl_port_stop(void)
{
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
