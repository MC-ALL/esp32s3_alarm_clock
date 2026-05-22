#include "app_module.h"
#include <app/app_config.h>
#include <app/audio_service.h>
#include <app/backlight_service.h>
#include <app/display_service.h>
#include <app/environment_service.h>
#include <app/module_common.h>
#include <app/net_service.h>
#include <app/presence_service.h>

#include <app/hw_config.h>
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
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <inttypes.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "display";

typedef enum {
	DISPLAY_PAGE_HOME = 0,
	DISPLAY_PAGE_SETTINGS,
	DISPLAY_PAGE_ALARM,
	DISPLAY_PAGE_NETWORK,
	DISPLAY_PAGE_POWER,
} display_page_t;

typedef enum {
	ALARM_VIEW_LIST = 0,
	ALARM_VIEW_ACTION,
	ALARM_VIEW_EDIT,
	ALARM_VIEW_DELETE_CONFIRM,
} alarm_view_t;

typedef enum {
	ALARM_ACTION_EDIT = 0,
	ALARM_ACTION_TOGGLE,
	ALARM_ACTION_DELETE,
	ALARM_ACTION_BACK,
} alarm_action_t;

typedef struct {
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
	bool repeat;
	bool enabled;
} display_alarm_item_t;

#define APP_DISPLAY_MAX_ALARMS 4U

static esp_lcd_panel_io_handle_t s_panel_io;
static esp_lcd_panel_handle_t s_panel;
static bool s_ready;
static bool s_spi_bus_owned;
static bool s_minimal_mode;
static bool s_minimal_greeted;
static bool s_env_notice_armed;
static char s_env_notice_text[32];
static int64_t s_last_presence_detected_us;
static int64_t s_presence_session_started_us;
static int64_t s_presence_last_absent_us;
static int64_t s_last_user_input_us;
static int64_t s_last_env_notice_us;
static int64_t s_boot_started_us;
static int64_t s_last_rest_notice_us;
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
static uint8_t s_settings_focus;
static uint8_t s_alarm_focus;
static uint8_t s_network_focus;
static bool s_settings_editing;
static bool s_network_selecting;
static bool s_setting_sound_on;
static bool s_setting_audio_on;
static uint8_t s_setting_volume;
static uint8_t s_setting_env_sample_s;
static uint8_t s_setting_repeat_count;
static uint8_t s_settings_edit_value;
static uint8_t s_network_action_focus;
static bool s_alarm_ringing;
static uint8_t s_alarm_ring_remaining;
static uint8_t s_alarm_ring_last_second;
static int64_t s_alarm_ring_last_play_us;
static display_alarm_item_t s_alarm_active_item;
static alarm_view_t s_alarm_view;
static alarm_action_t s_alarm_action_focus;
static uint8_t s_alarm_edit_field;
static bool s_alarm_edit_existing;
static uint8_t s_alarm_selected_index;
static display_alarm_item_t s_alarm_edit_item;
static display_alarm_item_t s_alarm_items[APP_DISPLAY_MAX_ALARMS];
static uint8_t s_alarm_count;
static char s_settings_body_text[192];
static char s_alarm_body_text[320];
static char s_network_body_text[320];
static char s_power_body_text[96];
static char s_last_time_text[16];
static char s_last_date_text[32];
static char s_last_time_state_text[16];
static char s_last_presence_text[16];
static char s_last_temp_text[16];
static char s_last_humi_text[16];
static char s_last_lux_text[16];
static char s_last_sound_text[16];
static char s_last_audio_text[16];
static char s_last_todo_title_text[16];
static char s_last_todo_item_1_text[96];
static char s_last_todo_item_2_text[96];
static char s_last_todo_item_3_text[96];
static char s_last_todo_more_text[16];

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

#if defined(LV_FONT_MONTSERRAT_16) && LV_FONT_MONTSERRAT_16
#define APP_TEXT_FONT (&lv_font_montserrat_16)
#else
#define APP_TEXT_FONT LV_FONT_DEFAULT
#endif

#ifndef LV_SYMBOL_SETTINGS
#define LV_SYMBOL_SETTINGS "Set"
#endif
#ifndef LV_SYMBOL_BELL
#define LV_SYMBOL_BELL "Alm"
#endif
#ifndef LV_SYMBOL_WIFI
#define LV_SYMBOL_WIFI "Net"
#endif
#ifndef LV_SYMBOL_POWER
#define LV_SYMBOL_POWER "Pwr"
#endif
#ifndef LV_SYMBOL_UP
#define LV_SYMBOL_UP "Up"
#endif
#ifndef LV_SYMBOL_DOWN
#define LV_SYMBOL_DOWN "Down"
#endif
#ifndef LV_SYMBOL_LEFT
#define LV_SYMBOL_LEFT "Back"
#endif
#ifndef LV_SYMBOL_RIGHT
#define LV_SYMBOL_RIGHT "Next"
#endif
#ifndef LV_SYMBOL_OK
#define LV_SYMBOL_OK "OK"
#endif
#ifndef LV_SYMBOL_SAVE
#define LV_SYMBOL_SAVE "Save"
#endif
#ifndef LV_SYMBOL_EDIT
#define LV_SYMBOL_EDIT "Edit"
#endif
#ifndef LV_SYMBOL_PLUS
#define LV_SYMBOL_PLUS "+"
#endif
#ifndef LV_SYMBOL_MINUS
#define LV_SYMBOL_MINUS "-"
#endif
#ifndef LV_SYMBOL_TRASH
#define LV_SYMBOL_TRASH "Del"
#endif
#ifndef LV_SYMBOL_STOP
#define LV_SYMBOL_STOP "Stop"
#endif
#ifndef LV_SYMBOL_HOME
#define LV_SYMBOL_HOME "Home"
#endif
#ifndef LV_SYMBOL_REFRESH
#define LV_SYMBOL_REFRESH "Sync"
#endif

static uint16_t s_frame_buffer[APP_LCD_WIDTH * 20];
static lv_color_t s_lvgl_buf1[APP_LCD_WIDTH * 20];
static lv_color_t s_lvgl_buf2[APP_LCD_WIDTH * 20];

/* Most 240x320 ST7789 modules need a small RAM window offset in portrait mode.
 */
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

	(void)esp_lcd_panel_draw_bitmap(panel, area->x1 + APP_LCD_X_GAP, area->y1 + APP_LCD_Y_GAP,
					area->x2 + 1 + APP_LCD_X_GAP, area->y2 + 1 + APP_LCD_Y_GAP, color_map);
	lv_display_flush_ready(disp);
}

#define APP_LV_SCREEN_ACTIVE() lv_screen_active()
#define APP_LV_TIMER_HANDLER() lv_timer_handler()
#else
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px_map)
{
	esp_lcd_panel_handle_t panel = drv->user_data;

	(void)esp_lcd_panel_draw_bitmap(panel, area->x1 + APP_LCD_X_GAP, area->y1 + APP_LCD_Y_GAP,
					area->x2 + 1 + APP_LCD_X_GAP, area->y2 + 1 + APP_LCD_Y_GAP, px_map);
	lv_disp_flush_ready(drv);
}

#define APP_LV_SCREEN_ACTIVE() lv_scr_act()
#define APP_LV_TIMER_HANDLER() lv_timer_handler()
#endif

static void display_apply_page_state(void);
static int display_play_audio_event(app_audio_event_t event_id);

static void disable_scroll(lv_obj_t *obj)
{
	if (obj == NULL) {
		return;
	}

	lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

static void style_single_line_label(lv_obj_t *label, int32_t width)
{
	if (label == NULL) {
		return;
	}

	disable_scroll(label);
	lv_obj_set_width(label, width);
	lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
}

static void style_panel(lv_obj_t *obj)
{
	disable_scroll(obj);
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_color(obj, lv_color_hex(0x444444), 0);
	lv_obj_set_style_border_width(obj, 0, 0);
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_pad_all(obj, 2, 0);
}

static void style_text_muted(lv_obj_t *obj)
{
	lv_obj_set_style_text_color(obj, lv_color_hex(0xBDBDBD), 0);
}

static void style_body_text(lv_obj_t *obj)
{
	lv_obj_set_style_text_font(obj, APP_TEXT_FONT, 0);
}

static void style_key_text(lv_obj_t *obj)
{
	lv_obj_set_style_text_font(obj, APP_TEXT_FONT, 0);
	lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
}

static void style_text_row_panel(lv_obj_t *obj)
{
	disable_scroll(obj);
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(obj, 0, 0);
	lv_obj_set_style_outline_width(obj, 0, 0);
	lv_obj_set_style_shadow_width(obj, 0, 0);
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

static void copy_display_text(char *out, size_t out_size, const char *text, size_t visible_chars)
{
	if (out == NULL || out_size == 0U) {
		return;
	}
	if (text == NULL) {
		out[0] = '\0';
		return;
	}

	const size_t len = strlen(text);
	if (len <= visible_chars || out_size <= 4U) {
		snprintf(out, out_size, "%s", text);
		return;
	}

	const size_t copy_len = visible_chars < (out_size - 4U) ? visible_chars : (out_size - 4U);
	memcpy(out, text, copy_len);
	memcpy(out + copy_len, "...", 4U);
}

static bool display_audio_event_enabled(app_audio_event_t event_id)
{
	switch (event_id) {
	case APP_AUDIO_EVENT_ALARM:
		return s_setting_audio_on;
	case APP_AUDIO_EVENT_WELCOME:
	case APP_AUDIO_EVENT_ENV_LIGHT_LOW:
	case APP_AUDIO_EVENT_ENV_LIGHT_HIGH:
	case APP_AUDIO_EVENT_ENV_TEMP_LOW:
	case APP_AUDIO_EVENT_ENV_TEMP_HIGH:
	case APP_AUDIO_EVENT_ENV_HUMI_LOW:
	case APP_AUDIO_EVENT_ENV_HUMI_HIGH:
	case APP_AUDIO_EVENT_CONFIRM:
	case APP_AUDIO_EVENT_REST_REMINDER:
	case APP_AUDIO_EVENT_WIFI_CONNECTED:
	case APP_AUDIO_EVENT_TEST:
	default:
		return s_setting_sound_on;
	}
}

static int display_play_audio_event(app_audio_event_t event_id)
{
	if (!display_audio_event_enabled(event_id)) {
		ESP_LOGD(TAG, "audio event skipped by ui setting event=%d", (int)event_id);
		return -1;
	}

	return audio_service_play_event(event_id);
}

static void display_enter_minimal_mode(void)
{
	s_minimal_mode = true;
	s_minimal_greeted = false;
}

static void display_exit_minimal_mode(bool trigger_welcome)
{
	if (s_minimal_mode && trigger_welcome && !s_minimal_greeted) {
		if (!audio_service_is_busy()) {
			(void)display_play_audio_event(APP_AUDIO_EVENT_WELCOME);
		}
		s_minimal_greeted = true;
	}
	s_minimal_mode = false;
}

static void display_update_minimal_mode(bool detected, bool radar_healthy)
{
	const int64_t now_us = esp_timer_get_time();
	if (detected) {
		s_last_presence_detected_us = now_us;
		if (s_presence_last_absent_us > 0 && (now_us - s_presence_last_absent_us) > 600000000LL) {
			s_presence_session_started_us = now_us;
		} else if (s_presence_session_started_us == 0) {
			s_presence_session_started_us = now_us;
		}
		s_presence_last_absent_us = 0;
	} else if (s_presence_last_absent_us == 0) {
		s_presence_last_absent_us = now_us;
	}

	const int64_t absence_us = detected ? 0 : (now_us - s_last_presence_detected_us);
	const int64_t inactivity_us = now_us - s_last_user_input_us;
	const bool should_enter_minimal =
	    !detected && radar_healthy && absence_us >= 30000000LL && inactivity_us >= 30000000LL;

	if (!s_minimal_mode && should_enter_minimal) {
		display_enter_minimal_mode();
	} else if (s_minimal_mode && detected && radar_healthy) {
		display_exit_minimal_mode(true);
	}
}

static void display_maybe_play_rest_notice(bool detected, bool radar_healthy)
{
	const int64_t now_us = esp_timer_get_time();

	if (!detected || !radar_healthy || s_presence_session_started_us == 0) {
		return;
	}
	if ((now_us - s_presence_session_started_us) < 10800000000LL) {
		return;
	}
	if ((now_us - s_last_rest_notice_us) < 300000000LL) {
		return;
	}

	if (!audio_service_is_busy() && display_play_audio_event(APP_AUDIO_EVENT_REST_REMINDER) == 0) {
		s_last_rest_notice_us = now_us;
		ESP_LOGI(TAG, "rest reminder triggered");
	}
}

static void display_maybe_play_env_notice(const app_environment_snapshot_t *snapshot)
{
	const int64_t now_us = esp_timer_get_time();
	const char *notice_text = NULL;
	if (snapshot == NULL) {
		return;
	}
	if ((now_us - s_last_env_notice_us) < 120000000LL) {
		return;
	}
	if ((now_us - s_boot_started_us) < 10000000LL) {
		return;
	}

	app_audio_event_t notice_event = APP_AUDIO_EVENT_ENV_LIGHT_LOW;
	if (snapshot->bh1750_valid && snapshot->lux < 100.0f) {
		notice_text = "LIGHT LOW";
		notice_event = APP_AUDIO_EVENT_ENV_LIGHT_LOW;
	} else if (snapshot->bh1750_valid && snapshot->lux > 600.0f) {
		notice_text = "LIGHT HIGH";
		notice_event = APP_AUDIO_EVENT_ENV_LIGHT_HIGH;
	} else if (snapshot->dht11_valid && snapshot->temperature_c > 30.0f) {
		notice_text = "TEMP HIGH";
		notice_event = APP_AUDIO_EVENT_ENV_TEMP_HIGH;
	} else if (snapshot->dht11_valid && snapshot->temperature_c < 10.0f) {
		notice_text = "TEMP LOW";
		notice_event = APP_AUDIO_EVENT_ENV_TEMP_LOW;
	} else if (snapshot->dht11_valid && snapshot->humidity_percent > 80.0f) {
		notice_text = "HUMI HIGH";
		notice_event = APP_AUDIO_EVENT_ENV_HUMI_HIGH;
	} else if (snapshot->dht11_valid && snapshot->humidity_percent < 10.0f) {
		notice_text = "HUMI LOW";
		notice_event = APP_AUDIO_EVENT_ENV_HUMI_LOW;
	}

	if (notice_text == NULL) {
		s_env_notice_text[0] = '\0';
		s_env_notice_armed = true;
		return;
	}
	snprintf(s_env_notice_text, sizeof(s_env_notice_text), "%s", notice_text);
	if (!s_env_notice_armed) {
		return;
	}

	if (!audio_service_is_busy() && display_play_audio_event(notice_event) == 0) {
		s_last_env_notice_us = now_us;
		s_env_notice_armed = false;
		ESP_LOGI(TAG, "env notice triggered: %s", s_env_notice_text);
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

static void build_subpage_container(lv_obj_t **page, lv_obj_t **body_out, lv_obj_t *screen, const char *title,
				    const char *body)
{
	lv_obj_t *title_label;
	lv_obj_t *body_label;

	if (page == NULL || screen == NULL) {
		return;
	}

	*page = lv_obj_create(screen);
	style_panel(*page);
	lv_obj_set_size(*page, 240, 320);
	lv_obj_align(*page, LV_ALIGN_TOP_LEFT, 0, 0);

	title_label = lv_label_create(*page);
	lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), 0);
	style_body_text(title_label);
	lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 0);
	lv_label_set_text(title_label, title);

	body_label = lv_label_create(*page);
	disable_scroll(body_label);
	lv_obj_set_style_text_color(body_label, lv_color_hex(0xFFFFFF), 0);
	style_body_text(body_label);
	lv_obj_set_width(body_label, 232);
	lv_label_set_long_mode(body_label, LV_LABEL_LONG_WRAP);
	lv_obj_align(body_label, LV_ALIGN_TOP_LEFT, 4, 28);
	lv_label_set_text(body_label, body);

	if (body_out != NULL) {
		*body_out = body_label;
	}
}

static bool display_alarm_matches_now(const display_alarm_item_t *alarm, const struct tm *timeinfo, bool time_valid)
{
	if (alarm == NULL || timeinfo == NULL || !time_valid || !alarm->enabled) {
		return false;
	}

	return alarm->hour == (uint8_t)timeinfo->tm_hour && alarm->minute == (uint8_t)timeinfo->tm_min &&
	       alarm->second == (uint8_t)timeinfo->tm_sec;
}

static void display_update_alarm_runtime(struct tm *timeinfo, bool time_valid)
{
	const int64_t now_us = esp_timer_get_time();

	if (!s_alarm_ringing) {
		for (uint8_t i = 0; i < s_alarm_count; i++) {
			if (!display_alarm_matches_now(&s_alarm_items[i], timeinfo, time_valid)) {
				continue;
			}

			if (s_alarm_ring_last_second == (uint8_t)timeinfo->tm_sec) {
				break;
			}

			s_alarm_ringing = true;
			s_alarm_active_item = s_alarm_items[i];
			s_alarm_ring_remaining = s_setting_repeat_count == 0U ? 1U : s_setting_repeat_count;
			s_alarm_ring_last_play_us = 0;
			s_alarm_ring_last_second = (uint8_t)timeinfo->tm_sec;
			s_current_page = DISPLAY_PAGE_POWER;
			snprintf(s_power_body_text, sizeof(s_power_body_text),
				 "%02u:%02u:%02u\n\nPRESS ANY KEY TO STOP", s_alarm_active_item.hour,
				 s_alarm_active_item.minute, s_alarm_active_item.second);
			lv_label_set_text(s_power_body_label, s_power_body_text);
			display_apply_page_state();
			break;
		}
	}

	if (!s_alarm_ringing) {
		return;
	}

	if (s_alarm_ring_remaining == 0U) {
		s_alarm_ringing = false;
		s_current_page = DISPLAY_PAGE_HOME;
		display_apply_page_state();
		return;
	}

	if ((now_us - s_alarm_ring_last_play_us) < 3200000LL) {
		return;
	}

	if (!audio_service_is_busy() && display_play_audio_event(APP_AUDIO_EVENT_ALARM) == 0) {
		s_alarm_ring_last_play_us = now_us;
		if (s_alarm_ring_remaining > 0U) {
			s_alarm_ring_remaining--;
		}
	}
}

static void display_update_settings_page(void)
{
	const uint8_t preview_volume =
	    (s_settings_editing && s_settings_focus == 2U) ? s_settings_edit_value : s_setting_volume;
	const uint8_t preview_env_sample =
	    (s_settings_editing && s_settings_focus == 3U) ? s_settings_edit_value : s_setting_env_sample_s;
	const uint8_t preview_repeat =
	    (s_settings_editing && s_settings_focus == 4U) ? s_settings_edit_value : s_setting_repeat_count;

	snprintf(s_settings_body_text, sizeof(s_settings_body_text),
		 "%s ALERTS:   %s\n%s ALARM:    %s\n%s VOLUME:   %u%s\n%s ENV RATE: "
		 "%us%s\n%s REPEAT:   %u%s\n\nMODE: %s",
		 s_settings_focus == 0U ? ">" : " ", s_setting_sound_on ? "ON" : "OFF",
		 s_settings_focus == 1U ? ">" : " ", s_setting_audio_on ? "ON" : "OFF",
		 s_settings_focus == 2U ? ">" : " ", (unsigned)preview_volume,
		 (s_settings_editing && s_settings_focus == 2U) ? " <" : "", s_settings_focus == 3U ? ">" : " ",
		 (unsigned)preview_env_sample, (s_settings_editing && s_settings_focus == 3U) ? " <" : "",
		 s_settings_focus == 4U ? ">" : " ", (unsigned)preview_repeat,
		 (s_settings_editing && s_settings_focus == 4U) ? " <" : "", s_settings_editing ? "EDIT" : "BROWSE");
	lv_label_set_text(s_settings_body_label, s_settings_body_text);
}

static void display_update_alarm_page(void)
{
	char list_buf[192];
	char detail_buf[96];
	const char *action_label = "";

	if (s_alarm_view == ALARM_VIEW_LIST) {
		size_t offset = 0;
		const bool alarm_full = s_alarm_count >= APP_DISPLAY_MAX_ALARMS;
		offset += (size_t)snprintf(list_buf + offset, sizeof(list_buf) - offset, "%s %s\n\n",
					   s_alarm_focus == 0U ? ">" : " ", alarm_full ? "ALARM FULL" : "+ NEW ALARM");
		offset += (size_t)snprintf(list_buf + offset, sizeof(list_buf) - offset, "  EXISTING ALARMS\n");
		for (uint8_t i = 0; i < s_alarm_count && offset < sizeof(list_buf); i++) {
			offset += (size_t)snprintf(
			    list_buf + offset, sizeof(list_buf) - offset, "%s %02u:%02u:%02u %s %s\n",
			    s_alarm_focus == (uint8_t)(i + 1U) ? ">" : " ", s_alarm_items[i].hour,
			    s_alarm_items[i].minute, s_alarm_items[i].second, s_alarm_items[i].repeat ? "REP" : "ONCE",
			    s_alarm_items[i].enabled ? "ON" : "OFF");
		}
		snprintf(s_alarm_body_text, sizeof(s_alarm_body_text), "%s\nSTATE: LIST", list_buf);
		lv_label_set_text(s_alarm_body_label, s_alarm_body_text);
		return;
	}

	if (s_alarm_selected_index < s_alarm_count) {
		snprintf(detail_buf, sizeof(detail_buf), "%02u:%02u:%02u  %s  %s",
			 s_alarm_items[s_alarm_selected_index].hour, s_alarm_items[s_alarm_selected_index].minute,
			 s_alarm_items[s_alarm_selected_index].second,
			 s_alarm_items[s_alarm_selected_index].repeat ? "REP" : "ONCE",
			 s_alarm_items[s_alarm_selected_index].enabled ? "ON" : "OFF");
	} else {
		snprintf(detail_buf, sizeof(detail_buf), "NEW ALARM");
	}

	if (s_alarm_view == ALARM_VIEW_ACTION) {
		static const char *ACTIONS[] = { "EDIT", "TOGGLE", "DELETE", "BACK" };
		snprintf(s_alarm_body_text, sizeof(s_alarm_body_text),
			 "ITEM: %s\n\n%s %s\n%s %s\n%s %s\n%s %s\n\nSTATE: ACTION", detail_buf,
			 s_alarm_action_focus == ALARM_ACTION_EDIT ? ">" : " ", ACTIONS[0],
			 s_alarm_action_focus == ALARM_ACTION_TOGGLE ? ">" : " ", ACTIONS[1],
			 s_alarm_action_focus == ALARM_ACTION_DELETE ? ">" : " ", ACTIONS[2],
			 s_alarm_action_focus == ALARM_ACTION_BACK ? ">" : " ", ACTIONS[3]);
		lv_label_set_text(s_alarm_body_label, s_alarm_body_text);
		return;
	}

	if (s_alarm_view == ALARM_VIEW_DELETE_CONFIRM) {
		snprintf(s_alarm_body_text, sizeof(s_alarm_body_text),
			 "DEL %s?\n\nK1 CANCEL\nK4 CONFIRM\n\nSTATE: DELETE", detail_buf);
		lv_label_set_text(s_alarm_body_label, s_alarm_body_text);
		return;
	}

	switch (s_alarm_edit_field) {
	case 0:
		action_label = "HOUR";
		break;
	case 1:
		action_label = "MIN";
		break;
	case 2:
		action_label = "SEC";
		break;
	case 3:
		action_label = "REPEAT";
		break;
	default:
		action_label = "ENABLED";
		break;
	}

	snprintf(s_alarm_body_text, sizeof(s_alarm_body_text),
		 "EDIT: %s\n\nHOUR:    %02u%s\nMINUTE:  %02u%s\nSECOND:  "
		 "%02u%s\nREPEAT:  %s%s\nENABLED: %s%s\n\nSTATE: EDIT",
		 action_label, s_alarm_edit_item.hour, s_alarm_edit_field == 0U ? " <" : "", s_alarm_edit_item.minute,
		 s_alarm_edit_field == 1U ? " <" : "", s_alarm_edit_item.second, s_alarm_edit_field == 2U ? " <" : "",
		 s_alarm_edit_item.repeat ? "YES" : "NO", s_alarm_edit_field == 3U ? " <" : "",
		 s_alarm_edit_item.enabled ? "ON" : "OFF", s_alarm_edit_field == 4U ? " <" : "");
	lv_label_set_text(s_alarm_body_label, s_alarm_body_text);
}

static void display_update_network_page(void)
{
	static const char *ITEMS[] = {
		"WIFI STATUS",
		"ACTION",
		"MY NET",
		"SYNC STATUS",
	};
	static const char *ACTION_ITEMS[] = {
		"CONNECT NOW",
		"SYNC TODO",
		"BACK",
	};
	char ssid_buf[16];
	char detail_buf[160];
	const char *state_text = "MODULE";
	app_net_status_t net_status = { 0 };
	app_todo_snapshot_t todo_snapshot = { 0 };

	(void)net_service_get_status(&net_status);
	(void)net_service_get_todo_snapshot(&todo_snapshot);

	if (s_network_focus == 0U) {
		copy_display_text(ssid_buf, sizeof(ssid_buf),
				  net_status.connected_ssid[0] != '\0' ? net_status.connected_ssid : "--", 10U);
		snprintf(detail_buf, sizeof(detail_buf), "SSID: %s\nSTARTED:   %s\nCONNECTED: %s\nIP READY:  %s",
			 ssid_buf, net_status.wifi_started ? "YES" : "NO", net_status.wifi_connected ? "YES" : "NO",
			 net_status.ip_ready ? "YES" : "NO");
	} else if (s_network_focus == 1U) {
		snprintf(detail_buf, sizeof(detail_buf), "%s %s\n%s %s\n%s %s",
			 s_network_action_focus == 0U ? ">" : " ", ACTION_ITEMS[0],
			 s_network_action_focus == 1U ? ">" : " ", ACTION_ITEMS[1],
			 s_network_action_focus == 2U ? ">" : " ", ACTION_ITEMS[2]);
	} else if (s_network_focus == 2U) {
		app_net_status_t scan_hint = { 0 };
		(void)net_service_get_status(&scan_hint);
		copy_display_text(ssid_buf, sizeof(ssid_buf), APP_WIFI_STA_SSID, 10U);
		if (scan_hint.wifi_connected &&
		    strncmp(scan_hint.connected_ssid, APP_WIFI_STA_SSID, sizeof(scan_hint.connected_ssid)) == 0) {
			snprintf(detail_buf, sizeof(detail_buf), "SSID: %s\nSTATE: CONNECTED\nIP:    %s", ssid_buf,
				 scan_hint.ip_addr[0] != '\0' ? scan_hint.ip_addr : "--");
		} else {
			snprintf(detail_buf, sizeof(detail_buf), "SSID: %s\nSTATE: NOT CONNECTED", ssid_buf);
		}
	} else {
		snprintf(detail_buf, sizeof(detail_buf),
			 "TIME SYNC: %s\nWIFI:      %s\nIP:        %s\nTODO:      "
			 "%s\nLAST:      %s",
			 net_status.time_synced ? "OK" : "WAIT", net_status.wifi_connected ? "ONLINE" : "OFFLINE",
			 net_status.ip_addr[0] != '\0' ? net_status.ip_addr : "--",
			 todo_snapshot.sync_in_progress ? "SYNC" : (todo_snapshot.sync_ok ? "OK" : "WAIT"),
			 todo_snapshot.last_sync_at[0] != '\0' ? todo_snapshot.last_sync_at : "--");
	}

	if (s_network_selecting) {
		state_text = "ACTION";
	}

	snprintf(s_network_body_text, sizeof(s_network_body_text), "MODULE: %s\n\n%s\n\nSTATE: %s",
		 ITEMS[s_network_focus], detail_buf, state_text);
	lv_label_set_text(s_network_body_label, s_network_body_text);
}

static void display_reset_subpage_state(display_page_t page)
{
	if (page == DISPLAY_PAGE_SETTINGS) {
		s_settings_focus = 0;
		s_settings_editing = false;
		display_update_settings_page();
	} else if (page == DISPLAY_PAGE_ALARM) {
		s_alarm_focus = 0;
		s_alarm_view = ALARM_VIEW_LIST;
		s_alarm_action_focus = ALARM_ACTION_EDIT;
		s_alarm_edit_field = 0;
		s_alarm_edit_existing = false;
		s_alarm_selected_index = 0;
		display_update_alarm_page();
	} else if (page == DISPLAY_PAGE_NETWORK) {
		s_network_focus = 0;
		s_network_selecting = false;
		s_network_action_focus = 0;
		display_update_network_page();
	} else if (page == DISPLAY_PAGE_POWER) {
		lv_obj_set_style_border_width(s_power_page, 0, 0);
		lv_obj_set_style_outline_width(s_power_page, 0, 0);
		lv_obj_set_style_shadow_width(s_power_page, 0, 0);
		snprintf(s_power_body_text, sizeof(s_power_body_text),
			 "Power off is not wired.\n\nOK shows a test confirm.");
		lv_label_set_text(s_power_body_label, s_power_body_text);
	}
}

static void display_enter_page(display_page_t page)
{
	s_current_page = page;
	if (page != DISPLAY_PAGE_HOME) {
		display_reset_subpage_state(page);
	}
}

static void display_apply_page_state(void)
{
	const bool home = (s_current_page == DISPLAY_PAGE_HOME) && !s_minimal_mode;

	set_obj_hidden(s_top_row, !home);
	set_obj_hidden(s_env_panel, !home);
	set_obj_hidden(s_tips_panel, !home);
	set_obj_hidden(s_alarm_panel, !home);
	set_obj_hidden(s_todo_panel, !home);
	set_obj_hidden(s_key_panel, s_minimal_mode);
	if (s_key_panel != NULL && !s_minimal_mode) {
		lv_obj_move_foreground(s_key_panel);
		lv_obj_set_style_border_width(s_key_panel, 0, 0);
		lv_obj_set_style_outline_width(s_key_panel, 0, 0);
		lv_obj_set_style_shadow_width(s_key_panel, 0, 0);
		lv_obj_set_style_bg_color(s_key_panel, lv_color_hex(0x000000), 0);
		lv_obj_set_style_bg_opa(s_key_panel, home ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
	}
	set_obj_hidden(s_settings_page, s_current_page != DISPLAY_PAGE_SETTINGS);
	set_obj_hidden(s_alarm_page, s_current_page != DISPLAY_PAGE_ALARM);
	set_obj_hidden(s_network_page, s_current_page != DISPLAY_PAGE_NETWORK);
	set_obj_hidden(s_power_page, s_current_page != DISPLAY_PAGE_POWER);

	if (home) {
		lv_label_set_text(s_key1_label, LV_SYMBOL_SETTINGS);
		lv_label_set_text(s_key2_label, LV_SYMBOL_BELL);
		lv_label_set_text(s_key3_label, LV_SYMBOL_WIFI);
		lv_label_set_text(s_key4_label, LV_SYMBOL_POWER);
		return;
	}

	if (s_minimal_mode) {
		return;
	}

	set_obj_hidden(s_key_panel, false);

	if (s_current_page == DISPLAY_PAGE_SETTINGS) {
		if (s_settings_editing) {
			lv_label_set_text(s_key1_label, LV_SYMBOL_PLUS);
			lv_label_set_text(s_key2_label, LV_SYMBOL_MINUS);
			lv_label_set_text(s_key3_label, LV_SYMBOL_SAVE);
			lv_label_set_text(s_key4_label, LV_SYMBOL_LEFT);
		} else {
			lv_label_set_text(s_key1_label, LV_SYMBOL_UP);
			lv_label_set_text(s_key2_label, LV_SYMBOL_DOWN);
			lv_label_set_text(s_key3_label, LV_SYMBOL_EDIT);
			lv_label_set_text(s_key4_label, LV_SYMBOL_HOME);
		}
		return;
	}

	if (s_current_page == DISPLAY_PAGE_ALARM) {
		if (s_alarm_view == ALARM_VIEW_LIST) {
			lv_label_set_text(s_key1_label, LV_SYMBOL_UP);
			lv_label_set_text(s_key2_label, LV_SYMBOL_DOWN);
			lv_label_set_text(s_key3_label, LV_SYMBOL_OK);
			lv_label_set_text(s_key4_label, LV_SYMBOL_HOME);
		} else if (s_alarm_view == ALARM_VIEW_ACTION) {
			lv_label_set_text(s_key1_label, LV_SYMBOL_UP);
			lv_label_set_text(s_key2_label, LV_SYMBOL_DOWN);
			lv_label_set_text(s_key3_label, LV_SYMBOL_OK);
			lv_label_set_text(s_key4_label, LV_SYMBOL_LEFT);
		} else if (s_alarm_view == ALARM_VIEW_DELETE_CONFIRM) {
			lv_label_set_text(s_key1_label, LV_SYMBOL_LEFT);
			lv_label_set_text(s_key2_label, "");
			lv_label_set_text(s_key3_label, "");
			lv_label_set_text(s_key4_label, LV_SYMBOL_TRASH);
		} else {
			lv_label_set_text(s_key1_label, LV_SYMBOL_MINUS);
			lv_label_set_text(s_key2_label, LV_SYMBOL_PLUS);
			lv_label_set_text(s_key3_label, LV_SYMBOL_RIGHT);
			lv_label_set_text(s_key4_label, LV_SYMBOL_LEFT);
		}
		return;
	}

	if (s_current_page == DISPLAY_PAGE_NETWORK) {
		if (s_network_selecting) {
			lv_label_set_text(s_key1_label, LV_SYMBOL_UP);
			lv_label_set_text(s_key2_label, LV_SYMBOL_DOWN);
			lv_label_set_text(s_key3_label, LV_SYMBOL_OK);
			lv_label_set_text(s_key4_label, LV_SYMBOL_LEFT);
		} else {
			lv_label_set_text(s_key1_label, LV_SYMBOL_LEFT);
			lv_label_set_text(s_key2_label, LV_SYMBOL_RIGHT);
			lv_label_set_text(s_key3_label, s_network_focus == 1U ? LV_SYMBOL_OK : "");
			lv_label_set_text(s_key4_label, LV_SYMBOL_HOME);
		}
		return;
	}

	if (s_current_page == DISPLAY_PAGE_POWER) {
		if (s_alarm_ringing) {
			lv_label_set_text(s_key1_label, LV_SYMBOL_STOP);
			lv_label_set_text(s_key2_label, LV_SYMBOL_STOP);
			lv_label_set_text(s_key3_label, LV_SYMBOL_STOP);
			lv_label_set_text(s_key4_label, LV_SYMBOL_STOP);
		} else {
			lv_label_set_text(s_key1_label, LV_SYMBOL_LEFT);
			lv_label_set_text(s_key2_label, "");
			lv_label_set_text(s_key3_label, "");
			lv_label_set_text(s_key4_label, LV_SYMBOL_OK);
		}
		return;
	}

	lv_label_set_text(s_key1_label, LV_SYMBOL_LEFT);
	lv_label_set_text(s_key2_label, "");
	lv_label_set_text(s_key3_label, "");
	lv_label_set_text(s_key4_label, "");
}

static void display_process_key_press(size_t key_index)
{
	if (s_current_page == DISPLAY_PAGE_HOME) {
		switch (key_index) {
		case 0:
			display_enter_page(DISPLAY_PAGE_SETTINGS);
			break;
		case 1:
			display_enter_page(DISPLAY_PAGE_ALARM);
			break;
		case 2:
			display_enter_page(DISPLAY_PAGE_NETWORK);
			break;
		case 3:
			display_enter_page(DISPLAY_PAGE_POWER);
			break;
		default:
			break;
		}
	} else if (s_current_page == DISPLAY_PAGE_SETTINGS) {
		if (s_settings_editing) {
			switch (key_index) {
			case 0:
				if (s_settings_focus == 2U && s_settings_edit_value < 10U) {
					s_settings_edit_value++;
				} else if (s_settings_focus == 3U && s_settings_edit_value < 30U) {
					s_settings_edit_value++;
				} else if (s_settings_focus == 4U && s_settings_edit_value < 9U) {
					s_settings_edit_value++;
				}
				display_update_settings_page();
				break;
			case 1:
				if (s_settings_focus == 2U && s_settings_edit_value > 0U) {
					s_settings_edit_value--;
				} else if (s_settings_focus == 3U && s_settings_edit_value > 2U) {
					s_settings_edit_value--;
				} else if (s_settings_focus == 4U && s_settings_edit_value > 0U) {
					s_settings_edit_value--;
				}
				display_update_settings_page();
				break;
			case 2:
				if (s_settings_focus == 2U) {
					s_setting_volume = s_settings_edit_value;
					(void)audio_service_set_volume(s_setting_volume);
				} else if (s_settings_focus == 3U) {
					s_setting_env_sample_s = s_settings_edit_value;
					(void)environment_service_set_sample_interval_s(s_setting_env_sample_s);
				} else if (s_settings_focus == 4U) {
					s_setting_repeat_count = s_settings_edit_value;
				}
				s_settings_editing = false;
				display_update_settings_page();
				break;
			case 3:
				s_settings_editing = false;
				display_update_settings_page();
				break;
			default:
				break;
			}
		} else {
			switch (key_index) {
			case 0:
				s_settings_focus = (uint8_t)((s_settings_focus + 4U) % 5U);
				display_update_settings_page();
				break;
			case 1:
				s_settings_focus = (uint8_t)((s_settings_focus + 1U) % 5U);
				display_update_settings_page();
				break;
			case 2:
				if (s_settings_focus == 0U) {
					s_setting_sound_on = !s_setting_sound_on;
				} else if (s_settings_focus == 1U) {
					s_setting_audio_on = !s_setting_audio_on;
				} else {
					s_settings_editing = true;
					if (s_settings_focus == 2U) {
						s_settings_edit_value = s_setting_volume;
					} else if (s_settings_focus == 3U) {
						s_settings_edit_value = s_setting_env_sample_s;
					} else {
						s_settings_edit_value = s_setting_repeat_count;
					}
				}
				display_update_settings_page();
				break;
			case 3:
				s_current_page = DISPLAY_PAGE_HOME;
				break;
			default:
				break;
			}
		}
	} else if (s_current_page == DISPLAY_PAGE_ALARM) {
		if (s_alarm_view == ALARM_VIEW_LIST) {
			const uint8_t item_count = (uint8_t)(s_alarm_count + 1U);
			switch (key_index) {
			case 0:
				s_alarm_focus = (uint8_t)((s_alarm_focus + item_count - 1U) % item_count);
				display_update_alarm_page();
				break;
			case 1:
				s_alarm_focus = (uint8_t)((s_alarm_focus + 1U) % item_count);
				display_update_alarm_page();
				break;
			case 2:
				if (s_alarm_focus == 0U) {
					if (s_alarm_count >= APP_DISPLAY_MAX_ALARMS) {
						break;
					}
					s_alarm_view = ALARM_VIEW_EDIT;
					s_alarm_edit_existing = false;
					s_alarm_selected_index = s_alarm_count;
					s_alarm_edit_field = 0;
					s_alarm_edit_item = (display_alarm_item_t){
						.hour = 7,
						.minute = 30,
						.second = 0,
						.repeat = true,
						.enabled = true,
					};
				} else {
					s_alarm_selected_index = (uint8_t)(s_alarm_focus - 1U);
					s_alarm_view = ALARM_VIEW_ACTION;
					s_alarm_action_focus = ALARM_ACTION_EDIT;
				}
				display_update_alarm_page();
				break;
			case 3:
				s_current_page = DISPLAY_PAGE_HOME;
				break;
			default:
				break;
			}
		} else if (s_alarm_view == ALARM_VIEW_ACTION) {
			switch (key_index) {
			case 0:
				s_alarm_action_focus = (alarm_action_t)((s_alarm_action_focus + 3U) % 4U);
				display_update_alarm_page();
				break;
			case 1:
				s_alarm_action_focus = (alarm_action_t)((s_alarm_action_focus + 1U) % 4U);
				display_update_alarm_page();
				break;
			case 2:
				if (s_alarm_selected_index >= s_alarm_count) {
					break;
				}
				if (s_alarm_action_focus == ALARM_ACTION_EDIT) {
					s_alarm_view = ALARM_VIEW_EDIT;
					s_alarm_edit_existing = true;
					s_alarm_edit_field = 0;
					s_alarm_edit_item = s_alarm_items[s_alarm_selected_index];
				} else if (s_alarm_action_focus == ALARM_ACTION_TOGGLE) {
					s_alarm_items[s_alarm_selected_index].enabled =
					    !s_alarm_items[s_alarm_selected_index].enabled;
					s_alarm_view = ALARM_VIEW_LIST;
					s_alarm_focus = (uint8_t)(s_alarm_selected_index + 1U);
				} else if (s_alarm_action_focus == ALARM_ACTION_DELETE) {
					s_alarm_view = ALARM_VIEW_DELETE_CONFIRM;
				} else {
					s_alarm_view = ALARM_VIEW_LIST;
					s_alarm_focus = (uint8_t)(s_alarm_selected_index + 1U);
				}
				display_update_alarm_page();
				break;
			case 3:
				s_alarm_view = ALARM_VIEW_LIST;
				s_alarm_focus = (uint8_t)(s_alarm_selected_index + 1U);
				display_update_alarm_page();
				break;
			default:
				break;
			}
		} else if (s_alarm_view == ALARM_VIEW_DELETE_CONFIRM) {
			if (key_index == 0U) {
				s_alarm_view = ALARM_VIEW_ACTION;
				display_update_alarm_page();
			} else if (key_index == 3U) {
				if (s_alarm_selected_index < s_alarm_count) {
					for (uint8_t i = s_alarm_selected_index; (i + 1U) < s_alarm_count; i++) {
						s_alarm_items[i] = s_alarm_items[i + 1U];
					}
					s_alarm_count--;
				}
				s_alarm_view = ALARM_VIEW_LIST;
				if (s_alarm_focus > s_alarm_count) {
					s_alarm_focus = s_alarm_count;
				}
				display_update_alarm_page();
			}
		} else {
			switch (key_index) {
			case 0:
				if (s_alarm_edit_field == 0U) {
					s_alarm_edit_item.hour = (uint8_t)((s_alarm_edit_item.hour + 23U) % 24U);
				} else if (s_alarm_edit_field == 1U) {
					s_alarm_edit_item.minute = (uint8_t)((s_alarm_edit_item.minute + 59U) % 60U);
				} else if (s_alarm_edit_field == 2U) {
					s_alarm_edit_item.second = (uint8_t)((s_alarm_edit_item.second + 59U) % 60U);
				} else if (s_alarm_edit_field == 3U) {
					s_alarm_edit_item.repeat = !s_alarm_edit_item.repeat;
				} else {
					s_alarm_edit_item.enabled = !s_alarm_edit_item.enabled;
				}
				display_update_alarm_page();
				break;
			case 1:
				if (s_alarm_edit_field == 0U) {
					s_alarm_edit_item.hour = (uint8_t)((s_alarm_edit_item.hour + 1U) % 24U);
				} else if (s_alarm_edit_field == 1U) {
					s_alarm_edit_item.minute = (uint8_t)((s_alarm_edit_item.minute + 1U) % 60U);
				} else if (s_alarm_edit_field == 2U) {
					s_alarm_edit_item.second = (uint8_t)((s_alarm_edit_item.second + 1U) % 60U);
				} else if (s_alarm_edit_field == 3U) {
					s_alarm_edit_item.repeat = !s_alarm_edit_item.repeat;
				} else {
					s_alarm_edit_item.enabled = !s_alarm_edit_item.enabled;
				}
				display_update_alarm_page();
				break;
			case 2:
				if (s_alarm_edit_field < 4U) {
					s_alarm_edit_field++;
					display_update_alarm_page();
				} else {
					if (s_alarm_edit_existing && s_alarm_selected_index < s_alarm_count) {
						s_alarm_items[s_alarm_selected_index] = s_alarm_edit_item;
						s_alarm_focus = (uint8_t)(s_alarm_selected_index + 1U);
					} else if (!s_alarm_edit_existing && s_alarm_count < APP_DISPLAY_MAX_ALARMS) {
						s_alarm_items[s_alarm_count] = s_alarm_edit_item;
						s_alarm_count++;
						s_alarm_focus = s_alarm_count;
					}
					s_alarm_view = ALARM_VIEW_LIST;
					display_update_alarm_page();
				}
				break;
			case 3:
				if (s_alarm_edit_existing) {
					s_alarm_view = ALARM_VIEW_ACTION;
				} else {
					s_alarm_view = ALARM_VIEW_LIST;
					s_alarm_focus = 0;
				}
				display_update_alarm_page();
				break;
			default:
				break;
			}
		}
	} else if (s_current_page == DISPLAY_PAGE_NETWORK) {
		if (s_network_selecting) {
			if (s_network_focus == 1U) {
				switch (key_index) {
				case 0:
					s_network_action_focus = (uint8_t)((s_network_action_focus + 2U) % 3U);
					display_update_network_page();
					break;
				case 1:
					s_network_action_focus = (uint8_t)((s_network_action_focus + 1U) % 3U);
					display_update_network_page();
					break;
				case 2:
					if (s_network_action_focus == 0U) {
						(void)net_service_request_connect_now();
						s_network_focus = 0;
						s_network_selecting = false;
					} else if (s_network_action_focus == 1U) {
						(void)net_service_request_todo_sync_now();
						s_network_focus = 3;
						s_network_selecting = false;
					} else {
						s_network_selecting = false;
					}
					display_update_network_page();
					break;
				case 3:
					s_network_selecting = false;
					display_update_network_page();
					break;
				default:
					break;
				}
			} else if (key_index == 3U) {
				s_network_selecting = false;
				display_update_network_page();
			}
		} else {
			switch (key_index) {
			case 0:
				s_network_focus = (uint8_t)((s_network_focus + 3U) % 4U);
				display_update_network_page();
				break;
			case 1:
				s_network_focus = (uint8_t)((s_network_focus + 1U) % 4U);
				display_update_network_page();
				break;
			case 2:
				if (s_network_focus == 1U) {
					s_network_selecting = true;
				}
				display_update_network_page();
				break;
			case 3:
				s_current_page = DISPLAY_PAGE_HOME;
				break;
			default:
				break;
			}
		}
	} else if (s_current_page == DISPLAY_PAGE_POWER) {
		if (key_index == 0U) {
			s_current_page = DISPLAY_PAGE_HOME;
		} else if (key_index == 3U && s_power_body_label != NULL) {
			snprintf(s_power_body_text, sizeof(s_power_body_text),
				 "Test confirmed.\nPower off is still not wired.");
			lv_label_set_text(s_power_body_label, s_power_body_text);
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
	lv_obj_set_style_pad_all(screen, 0, 0);
	disable_scroll(screen);

	s_top_row = lv_obj_create(screen);
	lv_obj_remove_style_all(s_top_row);
	disable_scroll(s_top_row);
	lv_obj_set_size(s_top_row, 240, 76);
	lv_obj_align(s_top_row, LV_ALIGN_TOP_LEFT, 0, 0);

	s_time_panel = lv_obj_create(s_top_row);
	style_panel(s_time_panel);
	lv_obj_set_size(s_time_panel, 156, 76);
	lv_obj_align(s_time_panel, LV_ALIGN_TOP_LEFT, 0, 0);

	s_date_label = lv_label_create(s_time_panel);
	style_text_muted(s_date_label);
	lv_obj_set_style_text_color(s_date_label, lv_color_hex(0x79A7FF), 0);
	style_body_text(s_date_label);
	style_single_line_label(s_date_label, 148);
	lv_obj_align(s_date_label, LV_ALIGN_TOP_LEFT, 0, 0);
	lv_label_set_text(s_date_label, "2026-05-15 FRI");

	s_time_state_label = lv_label_create(s_time_panel);
	style_text_muted(s_time_state_label);
	style_body_text(s_time_state_label);
	style_single_line_label(s_time_state_label, 70);
	lv_obj_align(s_time_state_label, LV_ALIGN_TOP_LEFT, 0, 19);
	lv_label_set_text(s_time_state_label, "");

	s_presence_label = lv_label_create(s_time_panel);
	style_text_muted(s_presence_label);
	style_body_text(s_presence_label);
	style_single_line_label(s_presence_label, 90);
	lv_obj_align(s_presence_label, LV_ALIGN_TOP_LEFT, 0, 38);
	lv_label_set_text(s_presence_label, "");

	s_time_label = lv_label_create(s_time_panel);
	lv_obj_set_style_text_font(s_time_label, APP_TIME_FONT, 0);
	lv_obj_set_style_text_letter_space(s_time_label, 1, 0);
	lv_obj_set_style_text_color(s_time_label, lv_color_hex(0xFFFFFF), 0);
	style_single_line_label(s_time_label, 156);
	lv_obj_align(s_time_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
	lv_label_set_text(s_time_label, "19:30:08");

	s_status_panel = lv_obj_create(s_top_row);
	style_panel(s_status_panel);
	lv_obj_set_size(s_status_panel, 80, 76);
	lv_obj_align(s_status_panel, LV_ALIGN_TOP_RIGHT, 0, 0);

	s_sound_label = lv_label_create(s_status_panel);
	style_body_text(s_sound_label);
	lv_obj_set_style_text_color(s_sound_label, lv_color_hex(0xFFFFFF), 0);
	style_single_line_label(s_sound_label, 72);
	lv_obj_align(s_sound_label, LV_ALIGN_TOP_LEFT, 0, 4);
	lv_label_set_text(s_sound_label, "ALT ON");

	s_audio_label = lv_label_create(s_status_panel);
	style_body_text(s_audio_label);
	lv_obj_set_style_text_color(s_audio_label, lv_color_hex(0xFFFFFF), 0);
	style_single_line_label(s_audio_label, 72);
	lv_obj_align(s_audio_label, LV_ALIGN_TOP_LEFT, 0, 30);
	lv_label_set_text(s_audio_label, "ALM ON");

	s_env_panel = lv_obj_create(screen);
	style_text_row_panel(s_env_panel);
	lv_obj_set_size(s_env_panel, 240, 22);
	lv_obj_align_to(s_env_panel, s_top_row, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

	s_temp_label = lv_label_create(s_env_panel);
	style_body_text(s_temp_label);
	lv_obj_set_style_text_color(s_temp_label, lv_color_hex(0xFFAB40), 0);
	style_single_line_label(s_temp_label, 74);
	lv_obj_align(s_temp_label, LV_ALIGN_LEFT_MID, 4, 0);
	lv_label_set_text(s_temp_label, "T 26C");

	s_humi_label = lv_label_create(s_env_panel);
	style_body_text(s_humi_label);
	lv_obj_set_style_text_color(s_humi_label, lv_color_hex(0x86D3FF), 0);
	style_single_line_label(s_humi_label, 72);
	lv_obj_align(s_humi_label, LV_ALIGN_LEFT_MID, 82, 0);
	lv_label_set_text(s_humi_label, "H 58%");

	s_lux_label = lv_label_create(s_env_panel);
	style_body_text(s_lux_label);
	lv_obj_set_style_text_color(s_lux_label, lv_color_hex(0xFFD54F), 0);
	style_single_line_label(s_lux_label, 82);
	lv_obj_align(s_lux_label, LV_ALIGN_LEFT_MID, 156, 0);
	lv_label_set_text(s_lux_label, "L 320");

	s_tips_panel = lv_obj_create(screen);
	style_text_row_panel(s_tips_panel);
	lv_obj_set_size(s_tips_panel, 240, 22);
	lv_obj_align_to(s_tips_panel, s_env_panel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

	s_tips_title_label = lv_label_create(s_tips_panel);
	lv_obj_add_flag(s_tips_title_label, LV_OBJ_FLAG_HIDDEN);
	lv_label_set_text(s_tips_title_label, "");

	s_tips_text_label = lv_label_create(s_tips_panel);
	style_body_text(s_tips_text_label);
	lv_obj_set_style_text_color(s_tips_text_label, lv_color_hex(0xFFFFFF), 0);
	style_single_line_label(s_tips_text_label, 232);
	lv_obj_align(s_tips_text_label, LV_ALIGN_LEFT_MID, 0, 0);
	lv_label_set_text(s_tips_text_label, "Light low");

	s_alarm_panel = lv_obj_create(screen);
	style_text_row_panel(s_alarm_panel);
	lv_obj_set_size(s_alarm_panel, 240, 22);
	lv_obj_align_to(s_alarm_panel, s_tips_panel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

	s_alarm_left_label = lv_label_create(s_alarm_panel);
	style_body_text(s_alarm_left_label);
	style_single_line_label(s_alarm_left_label, 126);
	lv_obj_align(s_alarm_left_label, LV_ALIGN_LEFT_MID, 4, 0);
	lv_label_set_text(s_alarm_left_label, "ALM 07:30");

	s_alarm_mode_label = lv_label_create(s_alarm_panel);
	style_text_muted(s_alarm_mode_label);
	style_body_text(s_alarm_mode_label);
	style_single_line_label(s_alarm_mode_label, 86);
	lv_obj_align(s_alarm_mode_label, LV_ALIGN_RIGHT_MID, -10, 0);
	lv_label_set_text(s_alarm_mode_label, "REPEAT");

	s_todo_panel = lv_obj_create(screen);
	style_panel(s_todo_panel);
	lv_obj_set_size(s_todo_panel, 240, 128);
	lv_obj_align_to(s_todo_panel, s_alarm_panel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

	s_todo_title_label = lv_label_create(s_todo_panel);
	style_text_muted(s_todo_title_label);
	style_body_text(s_todo_title_label);
	style_single_line_label(s_todo_title_label, 228);
	lv_obj_align(s_todo_title_label, LV_ALIGN_TOP_LEFT, 0, 0);
	lv_label_set_text(s_todo_title_label, "Todo List");

	s_todo_item_1_label = lv_label_create(s_todo_panel);
	lv_obj_set_style_text_color(s_todo_item_1_label, lv_color_hex(0xFFFFFF), 0);
	style_body_text(s_todo_item_1_label);
	style_single_line_label(s_todo_item_1_label, 228);
	lv_obj_align(s_todo_item_1_label, LV_ALIGN_TOP_LEFT, 0, 24);
	lv_label_set_text(s_todo_item_1_label, "1. Pick parcel");

	s_todo_item_2_label = lv_label_create(s_todo_panel);
	lv_obj_set_style_text_color(s_todo_item_2_label, lv_color_hex(0xFFFFFF), 0);
	style_body_text(s_todo_item_2_label);
	style_single_line_label(s_todo_item_2_label, 228);
	lv_obj_align(s_todo_item_2_label, LV_ALIGN_TOP_LEFT, 0, 47);
	lv_label_set_text(s_todo_item_2_label, "2. Send report");

	s_todo_more_label = lv_label_create(s_todo_panel);
	lv_obj_set_style_text_color(s_todo_more_label, lv_color_hex(0xFFFFFF), 0);
	style_body_text(s_todo_more_label);
	style_single_line_label(s_todo_more_label, 228);
	lv_obj_align(s_todo_more_label, LV_ALIGN_TOP_LEFT, 0, 70);
	lv_label_set_text(s_todo_more_label, "3. Team sync");

	s_env_label = lv_label_create(s_todo_panel);
	style_text_muted(s_env_label);
	style_body_text(s_env_label);
	style_single_line_label(s_env_label, 44);
	lv_obj_align(s_env_label, LV_ALIGN_TOP_LEFT, 0, 96);
	lv_label_set_text(s_env_label, "+3");

	s_key_panel = lv_obj_create(screen);
	style_text_row_panel(s_key_panel);
	lv_obj_set_size(s_key_panel, 240, 28);
	lv_obj_align_to(s_key_panel, s_todo_panel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

	s_key1_label = lv_label_create(s_key_panel);
	style_key_text(s_key1_label);
	style_single_line_label(s_key1_label, 50);
	lv_obj_align(s_key1_label, LV_ALIGN_LEFT_MID, 6, 0);
	lv_label_set_text(s_key1_label, LV_SYMBOL_SETTINGS);

	s_key2_label = lv_label_create(s_key_panel);
	style_key_text(s_key2_label);
	style_single_line_label(s_key2_label, 54);
	lv_obj_align(s_key2_label, LV_ALIGN_LEFT_MID, 64, 0);
	lv_label_set_text(s_key2_label, LV_SYMBOL_BELL);

	s_key3_label = lv_label_create(s_key_panel);
	style_key_text(s_key3_label);
	style_single_line_label(s_key3_label, 54);
	lv_obj_align(s_key3_label, LV_ALIGN_LEFT_MID, 124, 0);
	lv_label_set_text(s_key3_label, LV_SYMBOL_WIFI);

	s_key4_label = lv_label_create(s_key_panel);
	style_key_text(s_key4_label);
	style_single_line_label(s_key4_label, 56);
	lv_obj_align(s_key4_label, LV_ALIGN_LEFT_MID, 184, 0);
	lv_label_set_text(s_key4_label, LV_SYMBOL_POWER);

	build_subpage_container(&s_settings_page, &s_settings_body_label, screen, "SETTINGS", "");
	build_subpage_container(&s_alarm_page, &s_alarm_body_label, screen, "ALARM", "");
	build_subpage_container(&s_network_page, &s_network_body_label, screen, "NETWORK", "");
	build_subpage_container(&s_power_page, &s_power_body_label, screen, "POWER OFF?", "");
	set_obj_hidden(s_settings_page, true);
	set_obj_hidden(s_alarm_page, true);
	set_obj_hidden(s_network_page, true);
	set_obj_hidden(s_power_page, true);

	s_last_time_text[0] = '\0';
	s_last_sound_text[0] = '\0';
	s_last_audio_text[0] = '\0';
	s_last_todo_title_text[0] = '\0';
	s_last_date_text[0] = '\0';
	s_last_time_state_text[0] = '\0';
	s_last_presence_text[0] = '\0';
	s_last_temp_text[0] = '\0';
	s_last_humi_text[0] = '\0';
	s_last_lux_text[0] = '\0';
	s_last_todo_item_1_text[0] = '\0';
	s_last_todo_item_2_text[0] = '\0';
	s_last_todo_item_3_text[0] = '\0';
	s_last_todo_more_text[0] = '\0';
	s_setting_sound_on = true;
	s_setting_audio_on = true;
	s_setting_volume = 6;
	(void)audio_service_set_volume(s_setting_volume);
	s_setting_env_sample_s = (uint8_t)environment_service_get_sample_interval_s();
	(void)environment_service_set_sample_interval_s(s_setting_env_sample_s);
	s_setting_repeat_count = 2;
	s_alarm_items[0] =
	    (display_alarm_item_t){ .hour = 7, .minute = 30, .second = 0, .repeat = true, .enabled = true };
	s_alarm_items[1] = (display_alarm_item_t){ .hour = 8, .minute = 0, .second = 0, .repeat = true, .enabled = true };
	s_alarm_items[2] =
	    (display_alarm_item_t){ .hour = 20, .minute = 15, .second = 0, .repeat = false, .enabled = true };
	s_alarm_count = 3;
	s_alarm_ringing = false;
	s_alarm_ring_remaining = 0;
	s_alarm_ring_last_second = 255U;
	s_alarm_ring_last_play_us = 0;
	s_minimal_mode = false;
	s_minimal_greeted = false;
	s_env_notice_armed = true;
	s_boot_started_us = esp_timer_get_time();
	s_last_presence_detected_us = esp_timer_get_time();
	s_presence_session_started_us = 0;
	s_presence_last_absent_us = 0;
	s_last_user_input_us = esp_timer_get_time();
	s_last_env_notice_us = 0;
	s_last_rest_notice_us = 0;
	display_reset_subpage_state(DISPLAY_PAGE_SETTINGS);
	display_reset_subpage_state(DISPLAY_PAGE_ALARM);
	display_reset_subpage_state(DISPLAY_PAGE_NETWORK);
	display_reset_subpage_state(DISPLAY_PAGE_POWER);

	display_apply_page_state();
}

static void display_update_labels(void)
{
	char time_buf[16];
	char date_buf[32];
	char env_buf[96];
	app_environment_snapshot_t snapshot = { 0 };
	app_todo_snapshot_t todo_snapshot = { 0 };
	struct tm timeinfo = { 0 };
	const bool system_time_valid = get_display_time(&timeinfo);
	app_presence_status_t presence_status = { 0 };
	(void)presence_service_get_status(&presence_status);
	(void)net_service_get_todo_snapshot(&todo_snapshot);
	const bool detected = presence_status.detected;

	display_update_minimal_mode(detected, presence_status.radar_healthy);
	display_maybe_play_rest_notice(detected, presence_status.radar_healthy);
	display_update_alarm_runtime(&timeinfo, system_time_valid);

	snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
	snprintf(date_buf, sizeof(date_buf), "%04d-%02d-%02d %s", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
		 timeinfo.tm_mday, weekday_abbr(timeinfo.tm_wday));
	set_label_text_if_changed(s_time_label, s_last_time_text, sizeof(s_last_time_text), time_buf);
	set_label_text_if_changed(s_date_label, s_last_date_text, sizeof(s_last_date_text), date_buf);
	set_label_text_if_changed(s_time_state_label, s_last_time_state_text, sizeof(s_last_time_state_text),
				  system_time_valid ? "" : "UNSYNC");
	set_label_text_if_changed(s_presence_label, s_last_presence_text, sizeof(s_last_presence_text),
				  detected ? "DETECTED" : "");
	set_label_text_if_changed(s_sound_label, s_last_sound_text, sizeof(s_last_sound_text),
				  s_setting_sound_on ? "ALT ON" : "ALT OFF");
	set_label_text_if_changed(s_audio_label, s_last_audio_text, sizeof(s_last_audio_text),
				  s_setting_audio_on ? "ALM ON" : "ALM OFF");
	set_label_text_if_changed(s_tips_text_label, s_env_notice_text, sizeof(s_env_notice_text),
				  s_env_notice_text[0] != '\0' ? s_env_notice_text : "NORMAL");
	if (todo_snapshot.sync_in_progress) {
		lv_obj_set_style_text_color(s_todo_title_label, lv_color_hex(0x86D3FF), 0);
		set_label_text_if_changed(s_todo_title_label, s_last_todo_title_text, sizeof(s_last_todo_title_text),
					  "TODO SYNC");
	} else if (todo_snapshot.sync_ok) {
		style_text_muted(s_todo_title_label);
		set_label_text_if_changed(s_todo_title_label, s_last_todo_title_text, sizeof(s_last_todo_title_text),
					  "TODO OK");
	} else if (todo_snapshot.last_error[0] != '\0') {
		lv_obj_set_style_text_color(s_todo_title_label, lv_color_hex(0xFF6B6B), 0);
		set_label_text_if_changed(s_todo_title_label, s_last_todo_title_text, sizeof(s_last_todo_title_text),
					  "TODO ERR");
	} else {
		lv_obj_set_style_text_color(s_todo_title_label, lv_color_hex(0xFFD54F), 0);
		set_label_text_if_changed(s_todo_title_label, s_last_todo_title_text, sizeof(s_last_todo_title_text),
					  "TODO WAIT");
	}

	if (environment_service_get_snapshot(&snapshot)) {
		display_maybe_play_env_notice(&snapshot);
		snprintf(env_buf, sizeof(env_buf), "T-%s%.0fC", snapshot.dht11_valid ? "" : "-",
			 snapshot.dht11_valid ? snapshot.temperature_c : 0.0f);
		set_label_text_if_changed(s_temp_label, s_last_temp_text, sizeof(s_last_temp_text), env_buf);
		snprintf(env_buf, sizeof(env_buf), "H-%s%.0f%%", snapshot.dht11_valid ? "" : "-",
			 snapshot.dht11_valid ? snapshot.humidity_percent : 0.0f);
		set_label_text_if_changed(s_humi_label, s_last_humi_text, sizeof(s_last_humi_text), env_buf);
		snprintf(env_buf, sizeof(env_buf), "L-%s%.0f", snapshot.bh1750_valid ? "" : "-",
			 snapshot.bh1750_valid ? snapshot.lux : 0.0f);
		set_label_text_if_changed(s_lux_label, s_last_lux_text, sizeof(s_last_lux_text), env_buf);
	} else {
		set_label_text_if_changed(s_temp_label, s_last_temp_text, sizeof(s_last_temp_text), "T--C");
		set_label_text_if_changed(s_humi_label, s_last_humi_text, sizeof(s_last_humi_text), "H--%");
		set_label_text_if_changed(s_lux_label, s_last_lux_text, sizeof(s_last_lux_text), "L--");
	}

	if (todo_snapshot.count > 0U) {
		set_label_text_if_changed(s_todo_item_1_label, s_last_todo_item_1_text, sizeof(s_last_todo_item_1_text),
					  todo_snapshot.items[0].text);
		if (todo_snapshot.count > 1U) {
			set_label_text_if_changed(s_todo_item_2_label, s_last_todo_item_2_text,
						  sizeof(s_last_todo_item_2_text), todo_snapshot.items[1].text);
		} else {
			set_label_text_if_changed(s_todo_item_2_label, s_last_todo_item_2_text,
						  sizeof(s_last_todo_item_2_text), "");
		}
		if (todo_snapshot.count > 2U) {
			set_label_text_if_changed(s_todo_more_label, s_last_todo_item_3_text,
						  sizeof(s_last_todo_item_3_text), todo_snapshot.items[2].text);
		} else {
			set_label_text_if_changed(s_todo_more_label, s_last_todo_item_3_text,
						  sizeof(s_last_todo_item_3_text), "");
		}
		if (todo_snapshot.count > 3U) {
			snprintf(env_buf, sizeof(env_buf), "+%u", (unsigned)(todo_snapshot.count - 3U));
			set_label_text_if_changed(s_env_label, s_last_todo_more_text, sizeof(s_last_todo_more_text),
						  env_buf);
		} else {
			set_label_text_if_changed(s_env_label, s_last_todo_more_text, sizeof(s_last_todo_more_text),
						  "");
		}
	} else {
		set_label_text_if_changed(s_todo_item_1_label, s_last_todo_item_1_text, sizeof(s_last_todo_item_1_text),
					  "No todo");
		set_label_text_if_changed(s_todo_item_2_label, s_last_todo_item_2_text, sizeof(s_last_todo_item_2_text),
					  "");
		set_label_text_if_changed(s_todo_more_label, s_last_todo_item_3_text, sizeof(s_last_todo_item_3_text),
					  "");
		set_label_text_if_changed(s_env_label, s_last_todo_more_text, sizeof(s_last_todo_more_text), "");
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

	err = esp_lcd_panel_mirror(s_panel, false, false);
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

	s_key_event_queue = xQueueCreate(8, sizeof(size_t));
	if (s_key_event_queue == NULL) {
		ESP_LOGE(TAG, "failed to create display key queue");
		err = ESP_ERR_NO_MEM;
		goto fail;
	}

	if (xSemaphoreTake(s_lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
		display_build_boot_screen();
		(void)APP_LV_TIMER_HANDLER();
		xSemaphoreGive(s_lvgl_mutex);
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
	BaseType_t ok = xTaskCreate(display_task, "display_task", 6144, NULL, 7, &s_lvgl_task);
	if (ok != pdPASS) {
		ESP_LOGE(TAG, "failed to create display task");
		return -1;
	}

	return 0;
}

int display_service_stop(void)
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

	if (s_key_event_queue != NULL) {
		vQueueDelete(s_key_event_queue);
		s_key_event_queue = NULL;
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

	s_last_user_input_us = esp_timer_get_time();
	if (s_alarm_ringing) {
		s_alarm_ringing = false;
		s_alarm_ring_remaining = 0;
		s_current_page = DISPLAY_PAGE_HOME;
		display_apply_page_state();
		return;
	}
	if (s_minimal_mode) {
		display_exit_minimal_mode(false);
		display_apply_page_state();
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
