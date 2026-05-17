#include "app_module.h"
#include <app/backlight_service.h>
#include <app/display_service.h>
#include <app/environment_service.h>
#include <app/module_common.h>
#include <app/presence_service.h>

#include <app/hw_config.h>
#include <driver/spi_master.h>
#include <driver/spi_common.h>
#include <esp_err.h>
#include <esp_lcd_panel_dev.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <lvgl.h>

static const char *TAG = "display";

typedef enum {
	DISPLAY_PAGE_HOME = 0,
	DISPLAY_PAGE_SETTINGS,
	DISPLAY_PAGE_ALARM,
	DISPLAY_PAGE_NETWORK,
	DISPLAY_PAGE_POWER,
} display_page_t;

static esp_lcd_panel_io_handle_t s_panel_io;
static esp_lcd_panel_handle_t s_panel;
static bool s_ready;
static display_page_t s_current_page = DISPLAY_PAGE_HOME;
static SemaphoreHandle_t s_lvgl_mutex;
static TaskHandle_t s_lvgl_task;
static esp_timer_handle_t s_lvgl_tick_timer;
static QueueHandle_t s_key_event_queue;
static lv_obj_t *s_top_row;
static lv_obj_t *s_time_panel;
static lv_obj_t *s_status_panel;
static lv_obj_t *s_env_panel;
static lv_obj_t *s_tips_panel;
static lv_obj_t *s_alarm_panel;
static lv_obj_t *s_todo_panel;
static lv_obj_t *s_key_panel;
static lv_obj_t *s_settings_page;
static lv_obj_t *s_alarm_page;
static lv_obj_t *s_network_page;
static lv_obj_t *s_power_page;
static lv_obj_t *s_settings_body_label;
static lv_obj_t *s_alarm_body_label;
static lv_obj_t *s_network_body_label;
static lv_obj_t *s_power_body_label;
static lv_obj_t *s_date_label;
static lv_obj_t *s_time_label;
static lv_obj_t *s_time_state_label;
static lv_obj_t *s_presence_label;
static lv_obj_t *s_sound_label;
static lv_obj_t *s_audio_label;
static lv_obj_t *s_temp_label;
static lv_obj_t *s_humi_label;
static lv_obj_t *s_lux_label;
static lv_obj_t *s_tips_title_label;
static lv_obj_t *s_tips_text_label;
static lv_obj_t *s_alarm_left_label;
static lv_obj_t *s_alarm_mode_label;
static lv_obj_t *s_todo_title_label;
static lv_obj_t *s_todo_item_1_label;
static lv_obj_t *s_todo_item_2_label;
static lv_obj_t *s_todo_more_label;
static lv_obj_t *s_key1_label;
static lv_obj_t *s_key2_label;
static lv_obj_t *s_key3_label;
static lv_obj_t *s_key4_label;
static lv_obj_t *s_env_label;
static char s_last_time_text[16];
static char s_last_date_text[32];
static char s_last_time_state_text[16];
static char s_last_presence_text[16];
static char s_last_temp_text[16];
static char s_last_humi_text[16];
static char s_last_lux_text[16];

#if LVGL_VERSION_MAJOR >= 9
static lv_display_t *s_lvgl_display;
#else
static lv_disp_t *s_lvgl_display;
static lv_disp_draw_buf_t s_lvgl_draw_buf;
static lv_disp_drv_t s_lvgl_drv;
#endif

#if defined(LV_FONT_MONTSERRAT_28) && LV_FONT_MONTSERRAT_28
#define APP_TIME_FONT (&lv_font_montserrat_28)
#elif defined(LV_FONT_MONTSERRAT_24) && LV_FONT_MONTSERRAT_24
#define APP_TIME_FONT (&lv_font_montserrat_24)
#elif defined(LV_FONT_MONTSERRAT_20) && LV_FONT_MONTSERRAT_20
#define APP_TIME_FONT (&lv_font_montserrat_20)
#elif defined(LV_FONT_MONTSERRAT_18) && LV_FONT_MONTSERRAT_18
#define APP_TIME_FONT (&lv_font_montserrat_18)
#elif defined(LV_FONT_MONTSERRAT_16) && LV_FONT_MONTSERRAT_16
#define APP_TIME_FONT (&lv_font_montserrat_16)
#elif defined(LV_FONT_MONTSERRAT_14) && LV_FONT_MONTSERRAT_14
#define APP_TIME_FONT (&lv_font_montserrat_14)
#else
#define APP_TIME_FONT LV_FONT_DEFAULT
#endif

static uint16_t s_frame_buffer[APP_LCD_WIDTH * 20];
static lv_color_t s_lvgl_buf1[APP_LCD_WIDTH * 20];
static lv_color_t s_lvgl_buf2[APP_LCD_WIDTH * 20];

/* Most 240x320 ST7789 modules need a small RAM window offset in portrait mode. */
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
	lv_color_t *color_map = (lv_color_t *)px_map;

	(void)esp_lcd_panel_draw_bitmap(panel,
		area->x1 + APP_LCD_X_GAP,
		area->y1 + APP_LCD_Y_GAP,
		area->x2 + 1 + APP_LCD_X_GAP,
		area->y2 + 1 + APP_LCD_Y_GAP,
		color_map);
	lv_display_flush_ready(disp);
}

#define APP_LV_SCREEN_ACTIVE() lv_screen_active()
#define APP_LV_TIMER_HANDLER() lv_timer_handler()
#else
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px_map)
{
	esp_lcd_panel_handle_t panel = drv->user_data;

	(void)esp_lcd_panel_draw_bitmap(panel,
		area->x1 + APP_LCD_X_GAP,
		area->y1 + APP_LCD_Y_GAP,
		area->x2 + 1 + APP_LCD_X_GAP,
		area->y2 + 1 + APP_LCD_Y_GAP,
		px_map);
	lv_disp_flush_ready(drv);
}

#define APP_LV_SCREEN_ACTIVE() lv_scr_act()
#define APP_LV_TIMER_HANDLER() lv_timer_handler()
#endif

static void style_panel(lv_obj_t *obj)
{
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_color(obj, lv_color_hex(0x444444), 0);
	lv_obj_set_style_border_width(obj, 1, 0);
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_pad_all(obj, 4, 0);
}

static void style_text_muted(lv_obj_t *obj)
{
	lv_obj_set_style_text_color(obj, lv_color_hex(0xBDBDBD), 0);
}

static void style_row_text(lv_obj_t *obj)
{
	lv_obj_set_style_text_font(obj, LV_FONT_DEFAULT, 0);
}

static void style_text_row_panel(lv_obj_t *obj)
{
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(obj, 0, 0);
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_pad_all(obj, 4, 0);
}

static void set_label_text_if_changed(lv_obj_t *label, char *cache, size_t cache_size, const char *text)
{
	if (label == NULL || cache == NULL || text == NULL) {
		return;
	}

	if (strncmp(cache, text, cache_size) == 0) {
		return;
	}

	snprintf(cache, cache_size, "%s", text);
	lv_label_set_text(label, cache);
}

static void set_obj_hidden(lv_obj_t *obj, bool hidden)
{
	if (obj == NULL) {
		return;
	}

	if (hidden) {
		lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
	}
}

static bool get_display_time(struct tm *out_timeinfo)
{
	time_t now = 0;
	struct tm timeinfo = { 0 };

	if (out_timeinfo == NULL) {
		return false;
	}

	time(&now);
	localtime_r(&now, &timeinfo);
	if (timeinfo.tm_year > (2016 - 1900)) {
		*out_timeinfo = timeinfo;
		return true;
	}

	const uint64_t uptime_s = (uint64_t)(esp_timer_get_time() / 1000000ULL);
	timeinfo.tm_year = 2026 - 1900;
	timeinfo.tm_mon = 4;
	timeinfo.tm_mday = 15;
	timeinfo.tm_hour = (int)((uptime_s / 3600ULL) % 24ULL);
	timeinfo.tm_min = (int)((uptime_s / 60ULL) % 60ULL);
	timeinfo.tm_sec = (int)(uptime_s % 60ULL);
	timeinfo.tm_wday = 5;
	*out_timeinfo = timeinfo;
	return false;
}

static const char *weekday_abbr(int wday)
{
	static const char *WEEKDAYS[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };

	if (wday < 0 || wday > 6) {
		return "UNK";
	}

	return WEEKDAYS[wday];
}

static void build_subpage_container(lv_obj_t **page, lv_obj_t **body_out, lv_obj_t *screen, const char *title, const char *body)
{
	lv_obj_t *title_label;
	lv_obj_t *body_label;

	if (page == NULL || screen == NULL) {
		return;
	}

	*page = lv_obj_create(screen);
	style_panel(*page);
	lv_obj_set_size(*page, 224, 304);
	lv_obj_align(*page, LV_ALIGN_TOP_LEFT, 0, 0);

	title_label = lv_label_create(*page);
	lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), 0);
	lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 0);
	lv_label_set_text(title_label, title);

	body_label = lv_label_create(*page);
	lv_obj_set_style_text_color(body_label, lv_color_hex(0xFFFFFF), 0);
	lv_obj_set_width(body_label, 200);
	lv_label_set_long_mode(body_label, LV_LABEL_LONG_WRAP);
	lv_obj_align(body_label, LV_ALIGN_TOP_LEFT, 8, 28);
	lv_label_set_text(body_label, body);

	if (body_out != NULL) {
		*body_out = body_label;
	}
}

static void display_apply_page_state(void)
{
	const bool home = (s_current_page == DISPLAY_PAGE_HOME);

	set_obj_hidden(s_top_row, !home);
	set_obj_hidden(s_env_panel, !home);
	set_obj_hidden(s_tips_panel, !home);
	set_obj_hidden(s_alarm_panel, !home);
	set_obj_hidden(s_todo_panel, !home);
	set_obj_hidden(s_settings_page, s_current_page != DISPLAY_PAGE_SETTINGS);
	set_obj_hidden(s_alarm_page, s_current_page != DISPLAY_PAGE_ALARM);
	set_obj_hidden(s_network_page, s_current_page != DISPLAY_PAGE_NETWORK);
	set_obj_hidden(s_power_page, s_current_page != DISPLAY_PAGE_POWER);

	if (home) {
		set_obj_hidden(s_key_panel, false);
		lv_label_set_text(s_key1_label, "Set");
		lv_label_set_text(s_key2_label, "Alm");
		lv_label_set_text(s_key3_label, "Net");
		lv_label_set_text(s_key4_label, "Pwr");
		return;
	}

	set_obj_hidden(s_key_panel, false);

	if (s_current_page == DISPLAY_PAGE_POWER) {
		lv_label_set_text(s_key1_label, "Back");
		lv_label_set_text(s_key2_label, "");
		lv_label_set_text(s_key3_label, "");
		lv_label_set_text(s_key4_label, "Confirm");
		return;
	}

	lv_label_set_text(s_key1_label, "Set");
	lv_label_set_text(s_key2_label, "Alm");
	lv_label_set_text(s_key3_label, "Net");
	lv_label_set_text(s_key4_label, "Home");
}

static void display_process_key_press(size_t key_index)
{
	if (s_current_page == DISPLAY_PAGE_HOME) {
		switch (key_index) {
		case 0:
			s_current_page = DISPLAY_PAGE_SETTINGS;
			break;
		case 1:
			s_current_page = DISPLAY_PAGE_ALARM;
			break;
		case 2:
			s_current_page = DISPLAY_PAGE_NETWORK;
			break;
		case 3:
			s_current_page = DISPLAY_PAGE_POWER;
			break;
		default:
			break;
		}
	} else if (s_current_page == DISPLAY_PAGE_POWER) {
		if (key_index == 0U) {
			s_current_page = DISPLAY_PAGE_HOME;
		} else if (key_index == 3U && s_power_body_label != NULL) {
			lv_label_set_text(s_power_body_label, "Power off confirmed.\nHW action not wired yet.");
		}
	} else {
		switch (key_index) {
		case 0:
			s_current_page = DISPLAY_PAGE_SETTINGS;
			break;
		case 1:
			s_current_page = DISPLAY_PAGE_ALARM;
			break;
		case 2:
			s_current_page = DISPLAY_PAGE_NETWORK;
			break;
		case 3:
			s_current_page = DISPLAY_PAGE_HOME;
			break;
		default:
			break;
		}
	}

	display_apply_page_state();
}

static void display_build_boot_screen(void)
{
	lv_obj_t *screen = APP_LV_SCREEN_ACTIVE();

	lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
	lv_obj_set_style_text_color(screen, lv_color_hex(0xF5F5F5), 0);
	lv_obj_set_style_border_width(screen, 0, 0);
	lv_obj_set_style_pad_all(screen, 8, 0);

	s_top_row = lv_obj_create(screen);
	lv_obj_remove_style_all(s_top_row);
	lv_obj_set_size(s_top_row, 224, 76);
	lv_obj_align(s_top_row, LV_ALIGN_TOP_LEFT, 0, 0);

	s_time_panel = lv_obj_create(s_top_row);
	style_panel(s_time_panel);
	lv_obj_set_size(s_time_panel, 152, 76);
	lv_obj_align(s_time_panel, LV_ALIGN_TOP_LEFT, 0, 0);

	s_date_label = lv_label_create(s_time_panel);
	style_text_muted(s_date_label);
	lv_obj_set_style_text_color(s_date_label, lv_color_hex(0x79A7FF), 0);
	lv_obj_set_style_text_font(s_date_label, LV_FONT_DEFAULT, 0);
	lv_obj_align(s_date_label, LV_ALIGN_TOP_LEFT, 0, 0);
	lv_label_set_text(s_date_label, "2026-05-15 FRI");

	s_time_state_label = lv_label_create(s_time_panel);
	style_text_muted(s_time_state_label);
	lv_obj_align(s_time_state_label, LV_ALIGN_TOP_LEFT, 0, 18);
	lv_label_set_text(s_time_state_label, "");

	s_presence_label = lv_label_create(s_time_panel);
	style_text_muted(s_presence_label);
	lv_obj_align(s_presence_label, LV_ALIGN_TOP_LEFT, 0, 34);
	lv_label_set_text(s_presence_label, "");

	s_time_label = lv_label_create(s_time_panel);
	lv_obj_set_style_text_font(s_time_label, APP_TIME_FONT, 0);
	lv_obj_set_style_text_letter_space(s_time_label, 1, 0);
	lv_obj_set_style_text_color(s_time_label, lv_color_hex(0xFFFFFF), 0);
	lv_obj_align(s_time_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
	lv_label_set_text(s_time_label, "19:30:08");

	s_status_panel = lv_obj_create(s_top_row);
	style_panel(s_status_panel);
	lv_obj_set_size(s_status_panel, 66, 76);
	lv_obj_align(s_status_panel, LV_ALIGN_TOP_RIGHT, 0, 0);

	s_sound_label = lv_label_create(s_status_panel);
	style_row_text(s_sound_label);
	lv_obj_set_style_text_color(s_sound_label, lv_color_hex(0xFFFFFF), 0);
	lv_obj_align(s_sound_label, LV_ALIGN_TOP_LEFT, 0, 4);
	lv_label_set_text(s_sound_label, "SND ON");

	s_audio_label = lv_label_create(s_status_panel);
	style_row_text(s_audio_label);
	lv_obj_set_style_text_color(s_audio_label, lv_color_hex(0xFFFFFF), 0);
	lv_obj_align(s_audio_label, LV_ALIGN_TOP_LEFT, 0, 28);
	lv_label_set_text(s_audio_label, "AUD OFF");

	s_env_panel = lv_obj_create(screen);
	style_text_row_panel(s_env_panel);
	lv_obj_set_size(s_env_panel, 224, 22);
	lv_obj_align_to(s_env_panel, s_top_row, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

	s_temp_label = lv_label_create(s_env_panel);
	style_row_text(s_temp_label);
	lv_obj_set_style_text_color(s_temp_label, lv_color_hex(0xFFAB40), 0);
	lv_obj_align(s_temp_label, LV_ALIGN_LEFT_MID, 4, 0);
	lv_label_set_text(s_temp_label, "T 26C");

	s_humi_label = lv_label_create(s_env_panel);
	style_row_text(s_humi_label);
	lv_obj_set_style_text_color(s_humi_label, lv_color_hex(0x86D3FF), 0);
	lv_obj_align(s_humi_label, LV_ALIGN_LEFT_MID, 76, 0);
	lv_label_set_text(s_humi_label, "H 58%");

	s_lux_label = lv_label_create(s_env_panel);
	style_row_text(s_lux_label);
	lv_obj_set_style_text_color(s_lux_label, lv_color_hex(0xFFD54F), 0);
	lv_obj_align(s_lux_label, LV_ALIGN_LEFT_MID, 146, 0);
	lv_label_set_text(s_lux_label, "L 320");

	s_tips_panel = lv_obj_create(screen);
	style_text_row_panel(s_tips_panel);
	lv_obj_set_size(s_tips_panel, 224, 22);
	lv_obj_align_to(s_tips_panel, s_env_panel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

	s_tips_title_label = lv_label_create(s_tips_panel);
	lv_obj_add_flag(s_tips_title_label, LV_OBJ_FLAG_HIDDEN);
	lv_label_set_text(s_tips_title_label, "");

	s_tips_text_label = lv_label_create(s_tips_panel);
	style_row_text(s_tips_text_label);
	lv_obj_set_style_text_color(s_tips_text_label, lv_color_hex(0xFFFFFF), 0);
	lv_obj_set_width(s_tips_text_label, 216);
	lv_label_set_long_mode(s_tips_text_label, LV_LABEL_LONG_CLIP);
	lv_obj_align(s_tips_text_label, LV_ALIGN_LEFT_MID, 0, 0);
	lv_label_set_text(s_tips_text_label, "Light low");

	s_alarm_panel = lv_obj_create(screen);
	style_text_row_panel(s_alarm_panel);
	lv_obj_set_size(s_alarm_panel, 224, 22);
	lv_obj_align_to(s_alarm_panel, s_tips_panel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

	s_alarm_left_label = lv_label_create(s_alarm_panel);
	style_row_text(s_alarm_left_label);
	lv_obj_align(s_alarm_left_label, LV_ALIGN_LEFT_MID, 4, 0);
	lv_label_set_text(s_alarm_left_label, "ALM 07:30");

	s_alarm_mode_label = lv_label_create(s_alarm_panel);
	style_text_muted(s_alarm_mode_label);
	style_row_text(s_alarm_mode_label);
	lv_obj_align(s_alarm_mode_label, LV_ALIGN_RIGHT_MID, -10, 0);
	lv_label_set_text(s_alarm_mode_label, "REPEAT");

	s_todo_panel = lv_obj_create(screen);
	style_panel(s_todo_panel);
	lv_obj_set_size(s_todo_panel, 224, 108);
	lv_obj_align_to(s_todo_panel, s_alarm_panel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

	s_todo_title_label = lv_label_create(s_todo_panel);
	style_text_muted(s_todo_title_label);
	lv_obj_align(s_todo_title_label, LV_ALIGN_TOP_LEFT, 0, 0);
	lv_label_set_text(s_todo_title_label, "Todo List");

	s_todo_item_1_label = lv_label_create(s_todo_panel);
	lv_obj_set_style_text_color(s_todo_item_1_label, lv_color_hex(0xFFFFFF), 0);
	lv_obj_align(s_todo_item_1_label, LV_ALIGN_TOP_LEFT, 0, 22);
	lv_label_set_text(s_todo_item_1_label, "1. Pick parcel");

	s_todo_item_2_label = lv_label_create(s_todo_panel);
	lv_obj_set_style_text_color(s_todo_item_2_label, lv_color_hex(0xFFFFFF), 0);
	lv_obj_align(s_todo_item_2_label, LV_ALIGN_TOP_LEFT, 0, 42);
	lv_label_set_text(s_todo_item_2_label, "2. Send report");

	s_todo_more_label = lv_label_create(s_todo_panel);
	lv_obj_set_style_text_color(s_todo_more_label, lv_color_hex(0xFFFFFF), 0);
	lv_obj_align(s_todo_more_label, LV_ALIGN_TOP_LEFT, 0, 62);
	lv_label_set_text(s_todo_more_label, "3. Team sync");

	s_env_label = lv_label_create(s_todo_panel);
	style_text_muted(s_env_label);
	lv_obj_align(s_env_label, LV_ALIGN_TOP_LEFT, 0, 86);
	lv_label_set_text(s_env_label, "+3");

	s_key_panel = lv_obj_create(screen);
	style_text_row_panel(s_key_panel);
	lv_obj_set_size(s_key_panel, 224, 20);
	lv_obj_align_to(s_key_panel, s_todo_panel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

	s_key1_label = lv_label_create(s_key_panel);
	style_row_text(s_key1_label);
	lv_obj_align(s_key1_label, LV_ALIGN_LEFT_MID, 6, 0);
	lv_label_set_text(s_key1_label, "Set");

	s_key2_label = lv_label_create(s_key_panel);
	style_row_text(s_key2_label);
	lv_obj_align(s_key2_label, LV_ALIGN_LEFT_MID, 58, 0);
	lv_label_set_text(s_key2_label, "Alm");

	s_key3_label = lv_label_create(s_key_panel);
	style_row_text(s_key3_label);
	lv_obj_align(s_key3_label, LV_ALIGN_LEFT_MID, 112, 0);
	lv_label_set_text(s_key3_label, "Net");

	s_key4_label = lv_label_create(s_key_panel);
	style_row_text(s_key4_label);
	lv_obj_align(s_key4_label, LV_ALIGN_LEFT_MID, 164, 0);
	lv_label_set_text(s_key4_label, "Pwr");

	build_subpage_container(&s_settings_page, &s_settings_body_label, screen, "SETTINGS",
		"SOUND      ON\nAUDIO      OFF\nVOLUME     6\nENV SAMPLE 1s\nREPEAT     2");
	build_subpage_container(&s_alarm_page, &s_alarm_body_label, screen, "ALARM",
		"+ NEW ALARM\n07:30  REPEAT\n08:00  DAILY\n20:15  ONCE\n\nDETAIL 07:30\nSTATE  ENABLED");
	build_subpage_container(&s_network_page, &s_network_body_label, screen, "NETWORK",
		"WIFI  Connected\nSSID  Home-2.4G\nIP    192.168.1.72\n\nSCAN  Home/Office/Lab\nSYNC  TIME OK");
	build_subpage_container(&s_power_page, &s_power_body_label, screen, "POWER OFF?",
		"Press K4 to confirm.");
	set_obj_hidden(s_settings_page, true);
	set_obj_hidden(s_alarm_page, true);
	set_obj_hidden(s_network_page, true);
	set_obj_hidden(s_power_page, true);

	s_last_time_text[0] = '\0';
	s_last_date_text[0] = '\0';
	s_last_time_state_text[0] = '\0';
	s_last_presence_text[0] = '\0';
	s_last_temp_text[0] = '\0';
	s_last_humi_text[0] = '\0';
	s_last_lux_text[0] = '\0';

	display_apply_page_state();
}

static void display_update_labels(void)
{
	char time_buf[16];
	char date_buf[32];
	char env_buf[96];
	app_environment_snapshot_t snapshot = { 0 };
	struct tm timeinfo = { 0 };
	const bool system_time_valid = get_display_time(&timeinfo);
	const bool detected = presence_service_is_present_hint();

	snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d",
		timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
	snprintf(date_buf, sizeof(date_buf), "%04d-%02d-%02d %s",
		timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, weekday_abbr(timeinfo.tm_wday));
	set_label_text_if_changed(s_time_label, s_last_time_text, sizeof(s_last_time_text), time_buf);
	set_label_text_if_changed(s_date_label, s_last_date_text, sizeof(s_last_date_text), date_buf);
	set_label_text_if_changed(s_time_state_label, s_last_time_state_text, sizeof(s_last_time_state_text),
		system_time_valid ? "" : "UNSYNC");
	set_label_text_if_changed(s_presence_label, s_last_presence_text, sizeof(s_last_presence_text),
		detected ? "DETECTED" : "");

	if (environment_service_get_snapshot(&snapshot)) {
		snprintf(env_buf, sizeof(env_buf), "T-%s%.0fC",
			snapshot.dht11_valid ? "" : "-",
			snapshot.dht11_valid ? snapshot.temperature_c : 0.0f);
		set_label_text_if_changed(s_temp_label, s_last_temp_text, sizeof(s_last_temp_text), env_buf);
		snprintf(env_buf, sizeof(env_buf), "H-%s%.0f%%",
			snapshot.dht11_valid ? "" : "-",
			snapshot.dht11_valid ? snapshot.humidity_percent : 0.0f);
		set_label_text_if_changed(s_humi_label, s_last_humi_text, sizeof(s_last_humi_text), env_buf);
		snprintf(env_buf, sizeof(env_buf), "L-%s%.0f",
			snapshot.bh1750_valid ? "" : "-",
			snapshot.bh1750_valid ? snapshot.lux : 0.0f);
		set_label_text_if_changed(s_lux_label, s_last_lux_text, sizeof(s_last_lux_text), env_buf);
	} else {
		set_label_text_if_changed(s_temp_label, s_last_temp_text, sizeof(s_last_temp_text), "T--C");
		set_label_text_if_changed(s_humi_label, s_last_humi_text, sizeof(s_last_humi_text), "H--%");
		set_label_text_if_changed(s_lux_label, s_last_lux_text, sizeof(s_last_lux_text), "L--");
	}
}

static void display_task(void *arg)
{
	(void)arg;

	for (;;) {
		if (xSemaphoreTake(s_lvgl_mutex, portMAX_DELAY) == pdTRUE) {
			size_t key_index = 0;

			while (s_key_event_queue != NULL && xQueueReceive(s_key_event_queue, &key_index, 0) == pdTRUE) {
				display_process_key_press(key_index);
			}

			if (s_current_page == DISPLAY_PAGE_HOME) {
				display_update_labels();
			}
			(void)APP_LV_TIMER_HANDLER();
			xSemaphoreGive(s_lvgl_mutex);
		}

		vTaskDelay(pdMS_TO_TICKS(100));
	}
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
	if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
		ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)APP_LCD_SPI_HOST, &io_config, &s_panel_io);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_lcd_new_panel_io_spi failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	const esp_lcd_panel_dev_config_t panel_config = {
		.reset_gpio_num = APP_PIN_LCD_RST,
		.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
		.bits_per_pixel = 16,
	};

	err = esp_lcd_new_panel_st7789(s_panel_io, &panel_config, &s_panel);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_lcd_new_panel_st7789 failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	err = esp_lcd_panel_reset(s_panel);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_lcd_panel_reset failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	err = esp_lcd_panel_init(s_panel);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_lcd_panel_init failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	err = esp_lcd_panel_invert_color(s_panel, true);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_lcd_panel_invert_color failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	err = esp_lcd_panel_swap_xy(s_panel, false);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_lcd_panel_swap_xy failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	err = esp_lcd_panel_mirror(s_panel, false, false);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_lcd_panel_mirror failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	err = esp_lcd_panel_set_gap(s_panel, APP_LCD_X_GAP, APP_LCD_Y_GAP);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_lcd_panel_set_gap failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	err = esp_lcd_panel_disp_on_off(s_panel, true);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_lcd_panel_disp_on_off failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	lv_init();
	s_lvgl_mutex = xSemaphoreCreateMutex();
	if (s_lvgl_mutex == NULL) {
		ESP_LOGE(TAG, "failed to create lvgl mutex");
		return -1;
	}

#if LVGL_VERSION_MAJOR >= 9
	s_lvgl_display = lv_display_create(APP_LCD_WIDTH, APP_LCD_HEIGHT);
	lv_display_set_buffers(s_lvgl_display, s_lvgl_buf1, s_lvgl_buf2,
		sizeof(s_lvgl_buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
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
#endif

	const esp_timer_create_args_t tick_timer_args = {
		.callback = &lvgl_tick_cb,
		.name = "lvgl_tick",
	};
	err = esp_timer_create(&tick_timer_args, &s_lvgl_tick_timer);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_timer_create failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	err = esp_timer_start_periodic(s_lvgl_tick_timer, 2000);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_timer_start_periodic failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	if (xSemaphoreTake(s_lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
		display_build_boot_screen();
		(void)APP_LV_TIMER_HANDLER();
		xSemaphoreGive(s_lvgl_mutex);
	}

	s_ready = true;
	s_key_event_queue = xQueueCreate(8, sizeof(size_t));
	if (s_key_event_queue == NULL) {
		ESP_LOGE(TAG, "failed to create display key queue");
		return -1;
	}
	ESP_LOGI(TAG, "init lcd %dx%d", APP_LCD_WIDTH, APP_LCD_HEIGHT);
	return 0;
}

int display_service_start(void)
{
	BaseType_t ok = xTaskCreate(display_task, "display_task", 6144, NULL, 7, &s_lvgl_task);
	if (ok != pdPASS) {
		ESP_LOGE(TAG, "failed to create display task");
		return -1;
	}

	return 0;
}

int display_service_stop(void)
{
	if (!s_ready) {
		return 0;
	}

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

	if (s_key_event_queue != NULL) {
		vQueueDelete(s_key_event_queue);
		s_key_event_queue = NULL;
	}

	(void)esp_lcd_panel_disp_on_off(s_panel, false);
	if (s_panel != NULL) {
		(void)esp_lcd_panel_del(s_panel);
		s_panel = NULL;
	}
	if (s_panel_io != NULL) {
		(void)esp_lcd_panel_io_del(s_panel_io);
		s_panel_io = NULL;
	}
	(void)spi_bus_free(APP_LCD_SPI_HOST);
	s_ready = false;
	return 0;
}

bool display_service_is_ready(void)
{
	return s_ready;
}

void display_service_handle_key_press(size_t key_index)
{
	if (!s_ready || s_key_event_queue == NULL) {
		return;
	}

	(void)xQueueSend(s_key_event_queue, &key_index, 0);
}

int display_service_fill_color(uint16_t rgb565)
{
	if (!s_ready) {
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
