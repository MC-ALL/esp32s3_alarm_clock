#include "app_module.h"
#include <app/audio_service.h>
#include <app/display_service.h>
#include <app/environment_service.h>
#include <app/module_common.h>
#include <app/net_service.h>
#include <app/presence_service.h>
#include <app/settings_model.h>
#include <app/ui_model.h>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <inttypes.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "ui";

#define UI_SCREEN_W 240
#define UI_SCREEN_H 320
#define UI_CONTENT_H 240
#define UI_KEYBAR_Y 240
#define UI_KEYBAR_H 80
#define UI_TILE_W 120
#define UI_TILE_H 120
#define UI_KEY_W 60
#define UI_MAX_ALARMS 5U
#define UI_MAX_LOCAL_DONE 8U
#define UI_MAIN_PAGE_COUNT 6U
#define UI_KEY_COUNT 4U

#if defined(LV_FONT_MONTSERRAT_48) && LV_FONT_MONTSERRAT_48
#define UI_FONT_48 (&lv_font_montserrat_48)
#else
#define UI_FONT_48 LV_FONT_DEFAULT
#endif

#if defined(LV_FONT_MONTSERRAT_40) && LV_FONT_MONTSERRAT_40
#define UI_FONT_40 (&lv_font_montserrat_40)
#else
#define UI_FONT_40 UI_FONT_48
#endif

#if defined(LV_FONT_MONTSERRAT_36) && LV_FONT_MONTSERRAT_36
#define UI_FONT_36 (&lv_font_montserrat_36)
#else
#define UI_FONT_36 UI_FONT_40
#endif

#if defined(LV_FONT_MONTSERRAT_32) && LV_FONT_MONTSERRAT_32
#define UI_FONT_32 (&lv_font_montserrat_32)
#else
#define UI_FONT_32 UI_FONT_36
#endif

#if defined(LV_FONT_MONTSERRAT_30) && LV_FONT_MONTSERRAT_30
#define UI_FONT_30 (&lv_font_montserrat_30)
#else
#define UI_FONT_30 UI_FONT_32
#endif

#if defined(LV_FONT_MONTSERRAT_24) && LV_FONT_MONTSERRAT_24
#define UI_FONT_24 (&lv_font_montserrat_24)
#else
#define UI_FONT_24 UI_FONT_30
#endif

#if defined(LV_FONT_MONTSERRAT_20) && LV_FONT_MONTSERRAT_20
#define UI_FONT_20 (&lv_font_montserrat_20)
#else
#define UI_FONT_20 LV_FONT_DEFAULT
#endif

#if defined(LV_FONT_MONTSERRAT_16) && LV_FONT_MONTSERRAT_16
#define UI_FONT_16 (&lv_font_montserrat_16)
#else
#define UI_FONT_16 LV_FONT_DEFAULT
#endif

typedef enum {
	UI_PAGE_HOME = 0,
	UI_PAGE_ALARM,
	UI_PAGE_TODO,
	UI_PAGE_ENV,
	UI_PAGE_WIFI,
	UI_PAGE_LOW_CLOCK,
} ui_main_page_t;

typedef enum {
	UI_VIEW_MAIN = 0,
	UI_VIEW_HOME_SETTINGS,
	UI_VIEW_ALARM_SETTINGS,
	UI_VIEW_ALARM_ITEM,
	UI_VIEW_TODO_SETTINGS,
	UI_VIEW_ENV_SETTINGS,
	UI_VIEW_LOW_SETTINGS,
} ui_view_t;

typedef enum {
	UI_KEY_HOME_BACK = 0,
	UI_KEY_PREV_UP,
	UI_KEY_NEXT_DOWN,
	UI_KEY_OK,
} ui_key_t;

typedef struct {
	uint8_t hour;
	uint8_t minute;
	bool repeat;
	bool enabled;
	bool voice;
} ui_alarm_item_t;

static QueueHandle_t s_key_queue;
static TaskHandle_t s_ui_task;
static ui_main_page_t s_main_page = UI_PAGE_HOME;
static ui_view_t s_view = UI_VIEW_MAIN;
static uint8_t s_focus;
static uint8_t s_alarm_selected;
static uint8_t s_alarm_page_focus;
static uint8_t s_todo_page_focus;
static bool s_low_clock_auto_entered;
static bool s_home_clock_only;
static bool s_use_24h = true;
static bool s_low_use_24h = true;
static bool s_home_hour_chime_on = true;
static bool s_alarm_voice_on = true;
static bool s_todo_voice_on = true;
static bool s_env_voice_on = true;
static bool s_env_alert_on = true;
static uint8_t s_todo_refresh_min = 5;
static uint8_t s_env_sample_s = 10;
static int16_t s_env_temp_low_c = 10;
static int16_t s_env_temp_high_c = 35;
static uint16_t s_env_humi_low_percent = 30;
static uint16_t s_env_humi_high_percent = 80;
static uint16_t s_env_lux_low = 20;
static uint16_t s_env_lux_high = 1000;
static uint16_t s_low_enter_absent_s = 60;
static uint16_t s_low_exit_present_s = 3;
static ui_alarm_item_t s_alarms[UI_MAX_ALARMS];
static uint8_t s_alarm_count;
static char s_done_ids[UI_MAX_LOCAL_DONE][24];
static bool s_dirty = true;
static int s_last_render_second = -1;
static int64_t s_presence_absent_since_us;
static int64_t s_presence_present_since_us;

static lv_color_t c_black(void) { return lv_color_black(); }
static lv_color_t c_white(void) { return lv_color_white(); }
static lv_color_t c_blue(void) { return lv_color_hex(0x0057d8); }
static lv_color_t c_orange(void) { return lv_color_hex(0xc95400); }
static lv_color_t c_purple(void) { return lv_color_hex(0x5b2cbf); }
static lv_color_t c_teal(void) { return lv_color_hex(0x007a99); }
static lv_color_t c_green(void) { return lv_color_hex(0x16833a); }
static lv_color_t c_dark(void) { return lv_color_hex(0x101418); }
static lv_color_t c_gray(void) { return lv_color_hex(0x26313d); }
static lv_color_t c_red(void) { return lv_color_hex(0xc31828); }
static lv_color_t c_yellow(void) { return lv_color_hex(0xd19a00); }
static lv_color_t c_humi(void) { return lv_color_hex(0x006f6a); }
static lv_color_t c_lux(void) { return lv_color_hex(0x6e7300); }
static lv_color_t c_ip(void) { return lv_color_hex(0x245a7a); }
static lv_color_t c_sync(void) { return lv_color_hex(0x5d4e9d); }
static lv_color_t c_todo_net(void) { return lv_color_hex(0x476600); }
static lv_color_t c_panel(void) { return lv_color_hex(0x000000); }

static lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color,
		       int32_t x, int32_t y, int32_t w, int32_t h, lv_text_align_t align);

static void obj_plain(lv_obj_t *obj, lv_color_t bg)
{
	lv_obj_remove_style_all(obj);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(obj, bg, 0);
	lv_obj_set_style_border_width(obj, 0, 0);
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t bg)
{
	lv_obj_t *obj = lv_obj_create(parent);
	obj_plain(obj, bg);
	lv_obj_set_pos(obj, x, y);
	lv_obj_set_size(obj, w, h);
	return obj;
}

static lv_obj_t *scroll_area(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t bg)
{
	lv_obj_t *obj = box(parent, x, y, w, h, bg);
	lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_scroll_dir(obj, LV_DIR_VER);
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_ON);
	lv_obj_set_style_width(obj, 4, LV_PART_SCROLLBAR);
	lv_obj_set_style_radius(obj, 0, LV_PART_SCROLLBAR);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_SCROLLBAR);
	lv_obj_set_style_bg_color(obj, c_white(), LV_PART_SCROLLBAR);
	return obj;
}

static lv_obj_t *titled_scroll_body(lv_obj_t *screen, const char *title, lv_color_t bg)
{
	box(screen, 0, 0, UI_SCREEN_W, UI_CONTENT_H, bg);
	label(screen, title, UI_FONT_36, c_white(), 4, 4, 232, 44, LV_TEXT_ALIGN_CENTER);
	return scroll_area(screen, 0, 50, UI_SCREEN_W, UI_CONTENT_H - 50, bg);
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color,
		       int32_t x, int32_t y, int32_t w, int32_t h, lv_text_align_t align)
{
	lv_obj_t *obj = lv_label_create(parent);
	lv_obj_remove_style_all(obj);
	lv_obj_set_style_text_font(obj, font, 0);
	lv_obj_set_style_text_color(obj, color, 0);
	lv_obj_set_style_text_align(obj, align, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
	lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
	lv_label_set_text(obj, text);
	lv_obj_set_pos(obj, x, y);
	lv_obj_set_size(obj, w, h);
	return obj;
}

static lv_obj_t *label_wrap(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color,
			    int32_t x, int32_t y, int32_t w, int32_t h, lv_text_align_t align)
{
	lv_obj_t *obj = label(parent, text, font, color, x, y, w, h, align);
	lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
	return obj;
}

static void format_time(char *out, size_t out_size, bool use_24h, bool with_seconds)
{
	time_t now = 0;
	struct tm t = { 0 };
	time(&now);
	localtime_r(&now, &t);

	if (t.tm_year < (2024 - 1900)) {
		snprintf(out, out_size, with_seconds ? "--:--:--" : "--:--");
		return;
	}

	if (use_24h) {
		snprintf(out, out_size, with_seconds ? "%02d:%02d:%02d" : "%02d:%02d",
			 t.tm_hour, t.tm_min, t.tm_sec);
		return;
	}

	int hour = t.tm_hour % 12;
	if (hour == 0) {
		hour = 12;
	}
	snprintf(out, out_size, with_seconds ? "%02d:%02d:%02d" : "%02d:%02d", hour, t.tm_min, t.tm_sec);
}

static void format_date(char *out, size_t out_size)
{
	time_t now = 0;
	struct tm t = { 0 };
	time(&now);
	localtime_r(&now, &t);

	if (t.tm_year < (2024 - 1900)) {
		snprintf(out, out_size, "NO TIME");
		return;
	}

	snprintf(out, out_size, "%02d/%02d", t.tm_mon + 1, t.tm_mday);
}

static void format_alarm_time(const ui_alarm_item_t *alarm, char *out, size_t out_size)
{
	snprintf(out, out_size, "%02u:%02u", (unsigned)alarm->hour, (unsigned)alarm->minute);
}

static const ui_alarm_item_t *next_enabled_alarm(void)
{
	for (uint8_t i = 0; i < s_alarm_count; i++) {
		if (s_alarms[i].enabled) {
			return &s_alarms[i];
		}
	}
	return NULL;
}

static bool local_todo_done(const app_todo_item_t *item)
{
	if (item->done) {
		return true;
	}

	for (size_t i = 0; i < UI_MAX_LOCAL_DONE; i++) {
		if (s_done_ids[i][0] != '\0' && strcmp(s_done_ids[i], item->id) == 0) {
			return true;
		}
	}
	return false;
}

static void mark_todo_done(const app_todo_item_t *item)
{
	if (item == NULL || item->id[0] == '\0') {
		return;
	}

	for (size_t i = 0; i < UI_MAX_LOCAL_DONE; i++) {
		if (s_done_ids[i][0] == '\0' || strcmp(s_done_ids[i], item->id) == 0) {
			snprintf(s_done_ids[i], sizeof(s_done_ids[i]), "%s", item->id);
			ESP_LOGI(TAG, "todo marked done locally id=%s", item->id);
			return;
		}
	}

	snprintf(s_done_ids[0], sizeof(s_done_ids[0]), "%s", item->id);
	ESP_LOGI(TAG, "todo marked done locally id=%s overwrite=1", item->id);
}

static uint8_t todo_count(const app_todo_snapshot_t *snapshot)
{
	uint8_t count = 0;
	if (snapshot == NULL) {
		return 0;
	}

	for (uint8_t i = 0; i < snapshot->count && i < APP_TODO_MAX_ITEMS; i++) {
		if (!local_todo_done(&snapshot->items[i])) {
			count++;
		}
	}
	return count;
}

static void apply_settings(const app_settings_t *settings)
{
	if (settings == NULL) {
		return;
	}

	s_home_clock_only = settings->home_clock_only;
	s_use_24h = settings->use_24h;
	s_low_use_24h = settings->low_use_24h;
	s_home_hour_chime_on = settings->home_hour_chime_on;
	s_alarm_voice_on = settings->alarm_voice_on;
	s_todo_voice_on = settings->todo_voice_on;
	s_env_voice_on = settings->env_voice_on;
	s_env_alert_on = settings->env_alert_on;
	s_todo_refresh_min = settings->todo_refresh_min;
	s_env_sample_s = settings->env_sample_s;
	s_env_temp_low_c = settings->env_temp_low_c;
	s_env_temp_high_c = settings->env_temp_high_c;
	s_env_humi_low_percent = settings->env_humi_low_percent;
	s_env_humi_high_percent = settings->env_humi_high_percent;
	s_env_lux_low = settings->env_lux_low;
	s_env_lux_high = settings->env_lux_high;
	s_low_enter_absent_s = settings->low_enter_absent_s;
	s_low_exit_present_s = settings->low_exit_present_s;
	s_alarm_count = settings->alarm_count > UI_MAX_ALARMS ? UI_MAX_ALARMS : settings->alarm_count;

	for (uint8_t i = 0; i < s_alarm_count; i++) {
		s_alarms[i] = (ui_alarm_item_t){
			.hour = settings->alarms[i].hour,
			.minute = settings->alarms[i].minute,
			.repeat = settings->alarms[i].repeat,
			.enabled = settings->alarms[i].enabled,
			.voice = settings->alarms[i].voice,
		};
	}
	(void)environment_service_set_sample_interval_s(s_env_sample_s);
}

static void collect_settings(app_settings_t *settings)
{
	if (settings == NULL) {
		return;
	}

	settings_model_defaults(settings);
	settings->home_clock_only = s_home_clock_only;
	settings->use_24h = s_use_24h;
	settings->low_use_24h = s_low_use_24h;
	settings->home_hour_chime_on = s_home_hour_chime_on;
	settings->alarm_voice_on = s_alarm_voice_on;
	settings->todo_voice_on = s_todo_voice_on;
	settings->env_voice_on = s_env_voice_on;
	settings->env_alert_on = s_env_alert_on;
	settings->todo_refresh_min = s_todo_refresh_min;
	settings->env_sample_s = s_env_sample_s;
	settings->env_temp_low_c = s_env_temp_low_c;
	settings->env_temp_high_c = s_env_temp_high_c;
	settings->env_humi_low_percent = s_env_humi_low_percent;
	settings->env_humi_high_percent = s_env_humi_high_percent;
	settings->env_lux_low = s_env_lux_low;
	settings->env_lux_high = s_env_lux_high;
	settings->low_enter_absent_s = s_low_enter_absent_s;
	settings->low_exit_present_s = s_low_exit_present_s;
	settings->alarm_count = s_alarm_count > APP_SETTINGS_MAX_ALARMS ? APP_SETTINGS_MAX_ALARMS : s_alarm_count;

	for (uint8_t i = 0; i < settings->alarm_count; i++) {
		settings->alarms[i] = (app_alarm_setting_t){
			.hour = s_alarms[i].hour,
			.minute = s_alarms[i].minute,
			.repeat = s_alarms[i].repeat,
			.enabled = s_alarms[i].enabled,
			.voice = s_alarms[i].voice,
		};
	}
}

static void save_settings(void)
{
	app_settings_t settings = { 0 };
	collect_settings(&settings);
	int ret = settings_model_set(&settings);
	if (ret != 0) {
		ESP_LOGW(TAG, "settings save failed ret=%d", ret);
	}
}

static void sync_settings_from_model_for_main(void)
{
	if (s_view != UI_VIEW_MAIN) {
		return;
	}

	app_settings_t settings = { 0 };
	settings_model_get(&settings);
	apply_settings(&settings);
}

static void render_home(lv_obj_t *screen)
{
	char time_text[16];
	char date_text[16];
	app_todo_snapshot_t todo = { 0 };
	(void)net_service_get_todo_snapshot(&todo);
	format_time(time_text, sizeof(time_text), s_use_24h, false);
	format_date(date_text, sizeof(date_text));

	if (s_home_clock_only) {
		box(screen, 0, 0, UI_SCREEN_W, UI_CONTENT_H, c_blue());
		label(screen, time_text, UI_FONT_48, c_white(), 2, 62, 236, 64, LV_TEXT_ALIGN_CENTER);
		label(screen, date_text, UI_FONT_30, c_white(), 2, 134, 236, 40, LV_TEXT_ALIGN_CENTER);
		return;
	}

	box(screen, 0, 0, UI_SCREEN_W, UI_TILE_H, c_blue());
	label(screen, time_text, UI_FONT_48, c_white(), 2, 22, 236, 62, LV_TEXT_ALIGN_CENTER);
	label(screen, date_text, UI_FONT_24, c_white(), 2, 82, 236, 32, LV_TEXT_ALIGN_CENTER);

	box(screen, 0, UI_TILE_H, UI_TILE_W, UI_TILE_H, c_orange());
	label(screen, LV_SYMBOL_BELL, UI_FONT_32, c_white(), 2, UI_TILE_H + 10, 116, 36, LV_TEXT_ALIGN_CENTER);
	const ui_alarm_item_t *alarm = next_enabled_alarm();
	char alarm_text[16];
	if (alarm != NULL) {
		format_alarm_time(alarm, alarm_text, sizeof(alarm_text));
	} else {
		snprintf(alarm_text, sizeof(alarm_text), "--:--");
	}
	label(screen, alarm_text, UI_FONT_40, c_white(), 2, UI_TILE_H + 58, 116, 50, LV_TEXT_ALIGN_CENTER);

	box(screen, UI_TILE_W, UI_TILE_H, UI_TILE_W, UI_TILE_H, c_purple());
	label(screen, LV_SYMBOL_OK, UI_FONT_32, c_white(), UI_TILE_W + 2, UI_TILE_H + 10, 116, 36,
	      LV_TEXT_ALIGN_CENTER);
	char count_text[8];
	snprintf(count_text, sizeof(count_text), "%u", (unsigned)todo_count(&todo));
	label(screen, count_text, UI_FONT_40, c_white(), UI_TILE_W + 2, UI_TILE_H + 58, 116, 50,
	      LV_TEXT_ALIGN_CENTER);
}

static void render_alarm_page(lv_obj_t *screen)
{
	lv_obj_t *area = titled_scroll_body(screen, "ALARM", c_orange());
	if (s_alarm_count > 0U && s_alarm_page_focus >= s_alarm_count) {
		s_alarm_page_focus = 0;
	}

	for (uint8_t i = 0; i < s_alarm_count; i++) {
		char text[32];
		char time_text[16];
		format_alarm_time(&s_alarms[i], time_text, sizeof(time_text));
		snprintf(text, sizeof(text), "%s %s", time_text, s_alarms[i].enabled ? "ON" : "OFF");
		const bool selected = i == s_alarm_page_focus;
		lv_color_t bg = selected ? c_white() : lv_color_hex(0x9f4300);
		lv_color_t fg = selected ? c_black() : c_white();
		box(area, 4, 4 + i * 62, 228, 52, bg);
		label(area, text, UI_FONT_36, fg, 8, 11 + i * 62, 220, 42, LV_TEXT_ALIGN_CENTER);
	}

	if (s_alarm_count == 0U) {
		label(area, "NO ALARM", UI_FONT_40, c_white(), 4, 44, 232, 50, LV_TEXT_ALIGN_CENTER);
	}
	if (s_alarm_count > 0U) {
		lv_obj_scroll_to_y(area, s_alarm_page_focus * 62, LV_ANIM_OFF);
	}
}

static void render_todo_page(lv_obj_t *screen)
{
	app_todo_snapshot_t todo = { 0 };
	(void)net_service_get_todo_snapshot(&todo);
	lv_obj_t *area = titled_scroll_body(screen, "TODO", c_purple());

	uint8_t shown = 0;
	for (uint8_t i = 0; i < todo.count && i < APP_TODO_MAX_ITEMS; i++) {
		const bool selected = shown == s_todo_page_focus;
		lv_color_t bg = selected ? c_white() : lv_color_hex(0x3f1f86);
		lv_color_t fg = selected ? c_black() : c_white();
		box(area, 4, 4 + shown * 62, 228, 52, bg);
		label_wrap(area, todo.items[i].text[0] != '\0' ? todo.items[i].text : "(empty)", UI_FONT_24, fg,
			   8, 9 + shown * 62, 220, 42, LV_TEXT_ALIGN_LEFT);
		shown++;
	}

	if (shown == 0U) {
		const char *text = todo.sync_in_progress ? "SYNCING" : "NO TODO";
		label(area, text, UI_FONT_40, c_white(), 4, 44, 232, 50, LV_TEXT_ALIGN_CENTER);
	}
	if (shown > 0U) {
		if (s_todo_page_focus >= shown) {
			s_todo_page_focus = 0;
		}
		lv_obj_scroll_to_y(area, s_todo_page_focus * 62, LV_ANIM_OFF);
	}
}

static void render_env_page(lv_obj_t *screen)
{
	app_environment_snapshot_t env = { 0 };
	app_presence_status_t presence = { 0 };
	const bool ok = environment_service_get_snapshot(&env);
	const bool presence_ok = presence_service_get_status(&presence);
	char temp[16];
	char humi[16];
	char lux[16];
	char radar[16];
	snprintf(temp, sizeof(temp), ok && env.dht11_valid ? "%.0fC" : "--C", env.temperature_c);
	snprintf(humi, sizeof(humi), ok && env.dht11_valid ? "%.0f%%" : "--%%", env.humidity_percent);
	snprintf(lux, sizeof(lux), ok && env.bh1750_valid ? "%.0f" : "--", env.lux);
	snprintf(radar, sizeof(radar), presence_ok && presence.radar_healthy ? (presence.detected ? "PRES" : "NONE") :
									(presence.detected ? "OUT" : "ERR"));

	const bool temp_alert = s_env_alert_on && ok && env.dht11_valid &&
				(env.temperature_c < (float)s_env_temp_low_c ||
				 env.temperature_c > (float)s_env_temp_high_c);
	const bool humi_alert = s_env_alert_on && ok && env.dht11_valid &&
				(env.humidity_percent < (float)s_env_humi_low_percent ||
				 env.humidity_percent > (float)s_env_humi_high_percent);
	const bool lux_alert = s_env_alert_on && ok && env.bh1750_valid &&
			       (env.lux < (float)s_env_lux_low || env.lux > (float)s_env_lux_high);

	box(screen, 0, 0, UI_TILE_W, UI_TILE_H, temp_alert ? c_red() : c_green());
	label(screen, "TEMP", UI_FONT_24, c_white(), 2, 8, 116, 30, LV_TEXT_ALIGN_CENTER);
	label(screen, temp, UI_FONT_40, c_white(), 2, 58, 116, 50, LV_TEXT_ALIGN_CENTER);

	box(screen, UI_TILE_W, 0, UI_TILE_W, UI_TILE_H, humi_alert ? c_red() : c_humi());
	label(screen, "HUMI", UI_FONT_24, c_white(), UI_TILE_W + 2, 8, 116, 30, LV_TEXT_ALIGN_CENTER);
	label(screen, humi, UI_FONT_40, c_white(), UI_TILE_W + 2, 58, 116, 50, LV_TEXT_ALIGN_CENTER);

	box(screen, 0, UI_TILE_H, UI_TILE_W, UI_TILE_H, lux_alert ? c_red() : c_lux());
	label(screen, "LUX", UI_FONT_24, c_white(), 2, UI_TILE_H + 8, 116, 30, LV_TEXT_ALIGN_CENTER);
	label(screen, lux, UI_FONT_40, c_white(), 2, UI_TILE_H + 58, 116, 50, LV_TEXT_ALIGN_CENTER);

	box(screen, UI_TILE_W, UI_TILE_H, UI_TILE_W, UI_TILE_H, presence_ok && presence.radar_healthy ? c_red() : c_gray());
	label(screen, "RADAR", UI_FONT_24, c_white(), UI_TILE_W + 2, UI_TILE_H + 8, 116, 30, LV_TEXT_ALIGN_CENTER);
	label(screen, radar, UI_FONT_24, c_white(), UI_TILE_W + 2, UI_TILE_H + 58, 116, 50,
	      LV_TEXT_ALIGN_CENTER);
}

static void render_wifi_page(lv_obj_t *screen)
{
	app_net_status_t net = { 0 };
	app_todo_snapshot_t todo = { 0 };
	(void)net_service_get_status(&net);
	(void)net_service_get_todo_snapshot(&todo);

	box(screen, 0, 0, UI_TILE_W, UI_TILE_H, net.wifi_connected ? c_teal() : c_gray());
	label(screen, "WIFI", UI_FONT_24, c_white(), 2, 8, 116, 30, LV_TEXT_ALIGN_CENTER);
	label(screen, net.wifi_connected ? "ON" : "OFF", UI_FONT_24, c_white(), 2, 58, 116, 56, LV_TEXT_ALIGN_CENTER);

	box(screen, UI_TILE_W, 0, UI_TILE_W, UI_TILE_H, c_ip());
	label(screen, "IP", UI_FONT_24, c_white(), UI_TILE_W + 2, 8, 116, 30, LV_TEXT_ALIGN_CENTER);
	char ip_text[24];
	if (net.ip_ready) {
		unsigned a = 0;
		unsigned b = 0;
		unsigned c = 0;
		unsigned d = 0;
		if (sscanf(net.ip_addr, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
			snprintf(ip_text, sizeof(ip_text), "%u.%u\n%u.%u", a, b, c, d);
		} else {
			snprintf(ip_text, sizeof(ip_text), "%s", net.ip_addr);
		}
	} else {
		snprintf(ip_text, sizeof(ip_text), "--");
	}
	label(screen, ip_text, UI_FONT_24, c_white(), UI_TILE_W + 2, 52, 116, 62, LV_TEXT_ALIGN_CENTER);

	box(screen, 0, UI_TILE_H, UI_TILE_W, UI_TILE_H, net.time_synced ? c_sync() : c_orange());
	label(screen, "TIME", UI_FONT_24, c_white(), 2, UI_TILE_H + 8, 116, 30, LV_TEXT_ALIGN_CENTER);
	label(screen, net.time_synced ? "SYNC" : "WAIT", UI_FONT_24, c_white(), 2, UI_TILE_H + 58, 116, 56,
	      LV_TEXT_ALIGN_CENTER);

	box(screen, UI_TILE_W, UI_TILE_H, UI_TILE_W, UI_TILE_H, todo.sync_ok ? c_todo_net() : c_red());
	label(screen, "TODO", UI_FONT_24, c_white(), UI_TILE_W + 2, UI_TILE_H + 8, 116, 30, LV_TEXT_ALIGN_CENTER);
	label(screen, todo.sync_in_progress ? "..." : (todo.sync_ok ? "OK" : "ERR"), UI_FONT_24, c_white(),
	      UI_TILE_W + 2, UI_TILE_H + 58, 116, 56, LV_TEXT_ALIGN_CENTER);
}

static void render_low_clock(lv_obj_t *screen)
{
	char time_text[16];
	char date_text[16];
	format_time(time_text, sizeof(time_text), s_low_use_24h, true);
	format_date(date_text, sizeof(date_text));
	box(screen, 0, 0, UI_SCREEN_W, UI_CONTENT_H, c_black());
	label(screen, time_text, UI_FONT_40, c_white(), 2, 66, 236, 54, LV_TEXT_ALIGN_CENTER);
	label(screen, date_text, UI_FONT_24, c_gray(), 2, 132, 236, 32, LV_TEXT_ALIGN_CENTER);
}

static void render_row(lv_obj_t *parent, uint8_t row, const char *text, bool selected)
{
	const int32_t y = 4 + row * 44;
	box(parent, 4, y, 228, 38, selected ? c_yellow() : c_panel());
	label(parent, text, UI_FONT_24, selected ? c_black() : c_white(), 8, y + 5, 220, 30, LV_TEXT_ALIGN_LEFT);
}

static lv_obj_t *render_settings_area(lv_obj_t *screen, const char *title, lv_color_t bg)
{
	box(screen, 0, 0, UI_SCREEN_W, UI_CONTENT_H, bg);
	label(screen, title, UI_FONT_32, c_white(), 4, 4, 232, 40, LV_TEXT_ALIGN_CENTER);
	return scroll_area(screen, 0, 48, UI_SCREEN_W, UI_CONTENT_H - 48, bg);
}

static void scroll_focus_into_view(lv_obj_t *area, uint8_t focus)
{
	const int32_t row_y = 4 + focus * 44;
	int32_t scroll_y = row_y - 52;
	if (scroll_y < 0) {
		scroll_y = 0;
	}
	lv_obj_scroll_to_y(area, scroll_y, LV_ANIM_OFF);
}

static void render_home_settings(lv_obj_t *screen)
{
	lv_obj_t *area = render_settings_area(screen, "HOME SET", c_blue());
	render_row(area, 0, s_home_clock_only ? "LAYOUT CLOCK" : "LAYOUT FULL", s_focus == 0U);
	render_row(area, 1, s_use_24h ? "FORMAT 24H" : "FORMAT 12H", s_focus == 1U);
	render_row(area, 2, s_home_hour_chime_on ? "HOUR TONE ON" : "HOUR TONE OFF", s_focus == 2U);
	scroll_focus_into_view(area, s_focus);
}

static void render_alarm_settings(lv_obj_t *screen)
{
	lv_obj_t *area = render_settings_area(screen, "ALARM SET", c_orange());
	render_row(area, 0, s_alarm_voice_on ? "VOICE ON" : "VOICE OFF", s_focus == 0U);
	render_row(area, 1, "TEST VOICE", s_focus == 1U);
	render_row(area, 2, "ADD ALARM", s_focus == 2U);
	render_row(area, 3, "DELETE LAST", s_focus == 3U);

	for (uint8_t i = 0; i < s_alarm_count; i++) {
		char text[32];
		char time_text[16];
		format_alarm_time(&s_alarms[i], time_text, sizeof(time_text));
		snprintf(text, sizeof(text), "%u  %s %s", (unsigned)(i + 1), time_text,
			 s_alarms[i].enabled ? "ON" : "OFF");
		render_row(area, (uint8_t)(4U + i), text, s_focus == (uint8_t)(4U + i));
	}
	scroll_focus_into_view(area, s_focus);
}

static void render_alarm_item(lv_obj_t *screen)
{
	lv_obj_t *area = render_settings_area(screen, "ALARM ITEM", c_orange());
	ui_alarm_item_t *alarm = &s_alarms[s_alarm_selected];
	char time_text[24];
	format_alarm_time(alarm, time_text, sizeof(time_text));
	char row[32];
	snprintf(row, sizeof(row), "TIME %s", time_text);
	render_row(area, 0, row, s_focus == 0U);
	render_row(area, 1, alarm->repeat ? "REPEAT ON" : "REPEAT OFF", s_focus == 1U);
	render_row(area, 2, alarm->voice ? "VOICE ON" : "VOICE OFF", s_focus == 2U);
	render_row(area, 3, alarm->enabled ? "ENABLE ON" : "ENABLE OFF", s_focus == 3U);
	scroll_focus_into_view(area, s_focus);
}

static void render_todo_settings(lv_obj_t *screen)
{
	app_todo_snapshot_t todo = { 0 };
	(void)net_service_get_todo_snapshot(&todo);
	lv_obj_t *area = render_settings_area(screen, "TODO SET", c_purple());
	render_row(area, 0, "SYNC NOW", s_focus == 0U);
	char refresh[32];
	snprintf(refresh, sizeof(refresh), "REFRESH %uM", (unsigned)s_todo_refresh_min);
	render_row(area, 1, refresh, s_focus == 1U);
	render_row(area, 2, s_todo_voice_on ? "VOICE ON" : "VOICE OFF", s_focus == 2U);
	render_row(area, 3, "TEST VOICE", s_focus == 3U);

	const uint8_t todo_items = todo.count < APP_TODO_MAX_ITEMS ? todo.count : APP_TODO_MAX_ITEMS;
	for (uint8_t i = 0; i < todo_items; i++) {
		char text[48];
		snprintf(text, sizeof(text), "%s %.30s", local_todo_done(&todo.items[i]) ? "OK" : "DO",
			 todo.items[i].text);
		render_row(area, (uint8_t)(4U + i), text, s_focus == (uint8_t)(4U + i));
	}
	scroll_focus_into_view(area, s_focus);
}

static void render_env_settings(lv_obj_t *screen)
{
	lv_obj_t *area = render_settings_area(screen, "ENV SET", c_green());
	render_row(area, 0, s_env_voice_on ? "VOICE ON" : "VOICE OFF", s_focus == 0U);
	render_row(area, 1, "TEST VOICE", s_focus == 1U);
	char sample[32];
	snprintf(sample, sizeof(sample), "SAMPLE %uS", (unsigned)s_env_sample_s);
	render_row(area, 2, sample, s_focus == 2U);
	char row[32];
	snprintf(row, sizeof(row), "T LOW %dC", (int)s_env_temp_low_c);
	render_row(area, 3, row, s_focus == 3U);
	snprintf(row, sizeof(row), "T HIGH %dC", (int)s_env_temp_high_c);
	render_row(area, 4, row, s_focus == 4U);
	snprintf(row, sizeof(row), "H LOW %u%%", (unsigned)s_env_humi_low_percent);
	render_row(area, 5, row, s_focus == 5U);
	snprintf(row, sizeof(row), "H HIGH %u%%", (unsigned)s_env_humi_high_percent);
	render_row(area, 6, row, s_focus == 6U);
	snprintf(row, sizeof(row), "L LOW %u", (unsigned)s_env_lux_low);
	render_row(area, 7, row, s_focus == 7U);
	snprintf(row, sizeof(row), "L HIGH %u", (unsigned)s_env_lux_high);
	render_row(area, 8, row, s_focus == 8U);
	render_row(area, 9, s_env_alert_on ? "ALERT ON" : "ALERT OFF", s_focus == 9U);
	scroll_focus_into_view(area, s_focus);
}

static void render_low_settings(lv_obj_t *screen)
{
	lv_obj_t *area = render_settings_area(screen, "CLOCK SET", c_dark());
	char enter_text[32];
	char exit_text[32];
	snprintf(enter_text, sizeof(enter_text), "ENTER %uS", (unsigned)s_low_enter_absent_s);
	snprintf(exit_text, sizeof(exit_text), "EXIT %uS", (unsigned)s_low_exit_present_s);
	render_row(area, 0, s_low_use_24h ? "FORMAT 24H" : "FORMAT 12H", s_focus == 0U);
	render_row(area, 1, enter_text, s_focus == 1U);
	render_row(area, 2, exit_text, s_focus == 2U);
	scroll_focus_into_view(area, s_focus);
}

static const char *ok_icon_for_view(void)
{
	if (s_view == UI_VIEW_MAIN) {
		return s_main_page == UI_PAGE_WIFI ? LV_SYMBOL_CLOSE : LV_SYMBOL_SETTINGS;
	}
	return LV_SYMBOL_OK;
}

static void render_keybar(lv_obj_t *screen)
{
	const bool main_list_page = s_view == UI_VIEW_MAIN &&
				    (s_main_page == UI_PAGE_ALARM || s_main_page == UI_PAGE_TODO);
	const char *icons[UI_KEY_COUNT] = {
		s_view == UI_VIEW_MAIN ? (main_list_page ? LV_SYMBOL_DOWN : LV_SYMBOL_HOME) : LV_SYMBOL_LEFT,
		s_view == UI_VIEW_MAIN ? LV_SYMBOL_LEFT : LV_SYMBOL_UP,
		s_view == UI_VIEW_MAIN ? LV_SYMBOL_RIGHT : LV_SYMBOL_DOWN,
		ok_icon_for_view(),
	};

	for (size_t i = 0; i < UI_KEY_COUNT; i++) {
		box(screen, (int32_t)(i * UI_KEY_W), UI_KEYBAR_Y, UI_KEY_W, UI_KEYBAR_H, c_black());
		label(screen, icons[i], UI_FONT_32, c_white(), (int32_t)(i * UI_KEY_W), UI_KEYBAR_Y + 22,
		      UI_KEY_W, 40, LV_TEXT_ALIGN_CENTER);
	}
}

static void render_ui(void)
{
	lv_obj_t *screen = display_service_get_screen();
	if (screen == NULL) {
		return;
	}

	lv_obj_clean(screen);
	obj_plain(screen, c_black());
	lv_obj_set_size(screen, UI_SCREEN_W, UI_SCREEN_H);

	if (s_view == UI_VIEW_MAIN) {
		switch (s_main_page) {
		case UI_PAGE_HOME:
			render_home(screen);
			break;
		case UI_PAGE_ALARM:
			render_alarm_page(screen);
			break;
		case UI_PAGE_TODO:
			render_todo_page(screen);
			break;
		case UI_PAGE_ENV:
			render_env_page(screen);
			break;
		case UI_PAGE_WIFI:
			render_wifi_page(screen);
			break;
		case UI_PAGE_LOW_CLOCK:
			render_low_clock(screen);
			break;
		default:
			break;
		}
	} else if (s_view == UI_VIEW_HOME_SETTINGS) {
		render_home_settings(screen);
	} else if (s_view == UI_VIEW_ALARM_SETTINGS) {
		render_alarm_settings(screen);
	} else if (s_view == UI_VIEW_ALARM_ITEM) {
		render_alarm_item(screen);
	} else if (s_view == UI_VIEW_TODO_SETTINGS) {
		render_todo_settings(screen);
	} else if (s_view == UI_VIEW_ENV_SETTINGS) {
		render_env_settings(screen);
	} else if (s_view == UI_VIEW_LOW_SETTINGS) {
		render_low_settings(screen);
	}

	render_keybar(screen);
}

static uint8_t focus_count_for_view(void)
{
	app_todo_snapshot_t todo = { 0 };
	switch (s_view) {
	case UI_VIEW_HOME_SETTINGS:
		return 3;
	case UI_VIEW_ALARM_SETTINGS:
		return (uint8_t)(4U + s_alarm_count);
	case UI_VIEW_ALARM_ITEM:
		return 4;
	case UI_VIEW_TODO_SETTINGS:
		(void)net_service_get_todo_snapshot(&todo);
		return (uint8_t)(4U + (todo.count < APP_TODO_MAX_ITEMS ? todo.count : APP_TODO_MAX_ITEMS));
	case UI_VIEW_ENV_SETTINGS:
		return 10;
	case UI_VIEW_LOW_SETTINGS:
		return 3;
	default:
		return 1;
	}
}

static void enter_settings_for_page(void)
{
	s_focus = 0;
	switch (s_main_page) {
	case UI_PAGE_HOME:
		s_view = UI_VIEW_HOME_SETTINGS;
		break;
	case UI_PAGE_ALARM:
		s_view = UI_VIEW_ALARM_SETTINGS;
		break;
	case UI_PAGE_TODO:
		s_view = UI_VIEW_TODO_SETTINGS;
		break;
	case UI_PAGE_ENV:
		s_view = UI_VIEW_ENV_SETTINGS;
		break;
	case UI_PAGE_WIFI:
		ESP_LOGI(TAG, "wifi page has no settings");
		break;
	case UI_PAGE_LOW_CLOCK:
		s_view = UI_VIEW_LOW_SETTINGS;
		break;
	default:
		break;
	}
}

static void play_voice_test(const app_audio_event_t *events, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		int ret = audio_service_play_event(events[i]);
		ESP_LOGI(TAG, "voice test event=%d ret=%d", (int)events[i], ret);
	}
}

static void play_alarm_voice_test(void)
{
	const app_audio_event_t events[] = { APP_AUDIO_EVENT_ALARM };
	play_voice_test(events, sizeof(events) / sizeof(events[0]));
}

static void play_todo_voice_test(void)
{
	const app_audio_event_t events[] = { APP_AUDIO_EVENT_REST_REMINDER };
	play_voice_test(events, sizeof(events) / sizeof(events[0]));
}

static void play_env_voice_test(void)
{
	const app_audio_event_t events[] = {
		APP_AUDIO_EVENT_ENV_LIGHT_LOW,
		APP_AUDIO_EVENT_ENV_LIGHT_HIGH,
		APP_AUDIO_EVENT_ENV_TEMP_LOW,
		APP_AUDIO_EVENT_ENV_TEMP_HIGH,
		APP_AUDIO_EVENT_ENV_HUMI_LOW,
		APP_AUDIO_EVENT_ENV_HUMI_HIGH,
	};
	play_voice_test(events, sizeof(events) / sizeof(events[0]));
}

static void update_low_clock_presence_runtime(void)
{
	app_presence_status_t presence = { 0 };
	if (!presence_service_get_status(&presence)) {
		return;
	}

	const int64_t now_us = esp_timer_get_time();
	if (presence.detected) {
		s_presence_absent_since_us = 0;
		if (s_presence_present_since_us == 0) {
			s_presence_present_since_us = now_us;
		}
	} else {
		s_presence_present_since_us = 0;
		if (s_presence_absent_since_us == 0) {
			s_presence_absent_since_us = now_us;
		}
	}

	if (s_view != UI_VIEW_MAIN) {
		return;
	}

	if (s_main_page != UI_PAGE_LOW_CLOCK && !presence.detected && s_presence_absent_since_us > 0) {
		const int64_t absent_s = (now_us - s_presence_absent_since_us) / 1000000LL;
		if (absent_s >= (int64_t)s_low_enter_absent_s) {
			s_main_page = UI_PAGE_LOW_CLOCK;
			s_low_clock_auto_entered = true;
			s_dirty = true;
			ESP_LOGI(TAG, "enter low clock absent_s=%" PRIi64 " threshold=%u healthy=%d fallback=%d",
				 absent_s,
				 (unsigned)s_low_enter_absent_s,
				 presence.radar_healthy ? 1 : 0,
				 presence.using_out_fallback ? 1 : 0);
		}
		return;
	}

	if (s_main_page == UI_PAGE_LOW_CLOCK && s_low_clock_auto_entered && presence.detected &&
	    s_presence_present_since_us > 0) {
		const int64_t present_s = (now_us - s_presence_present_since_us) / 1000000LL;
		if (present_s >= (int64_t)s_low_exit_present_s) {
			s_main_page = UI_PAGE_HOME;
			s_low_clock_auto_entered = false;
			s_dirty = true;
			ESP_LOGI(TAG, "exit low clock present_s=%" PRIi64 " threshold=%u healthy=%d fallback=%d",
				 present_s,
				 (unsigned)s_low_exit_present_s,
				 presence.radar_healthy ? 1 : 0,
				 presence.using_out_fallback ? 1 : 0);
		}
	}
}

static void add_alarm(void)
{
	if (s_alarm_count >= UI_MAX_ALARMS) {
		ESP_LOGW(TAG, "alarm list full");
		return;
	}

	s_alarms[s_alarm_count] = (ui_alarm_item_t){
		.hour = 7,
		.minute = 30,
		.repeat = true,
		.enabled = true,
		.voice = true,
	};
	s_alarm_selected = s_alarm_count;
	s_alarm_count++;
	s_view = UI_VIEW_ALARM_ITEM;
	s_focus = 0;
	save_settings();
	ESP_LOGI(TAG, "alarm added count=%u", (unsigned)s_alarm_count);
}

static void delete_last_alarm(void)
{
	if (s_alarm_count == 0U) {
		return;
	}

	s_alarm_count--;
	if (s_alarm_selected >= s_alarm_count) {
		s_alarm_selected = s_alarm_count == 0U ? 0U : (uint8_t)(s_alarm_count - 1U);
	}
	save_settings();
	ESP_LOGI(TAG, "alarm deleted count=%u", (unsigned)s_alarm_count);
}

static void process_ok_in_settings(void)
{
	if (s_view == UI_VIEW_HOME_SETTINGS) {
		if (s_focus == 0U) {
			s_home_clock_only = !s_home_clock_only;
		} else if (s_focus == 1U) {
			s_use_24h = !s_use_24h;
		} else {
			s_home_hour_chime_on = !s_home_hour_chime_on;
		}
		save_settings();
		return;
	}

	if (s_view == UI_VIEW_ALARM_SETTINGS) {
		if (s_focus == 0U) {
			s_alarm_voice_on = !s_alarm_voice_on;
			save_settings();
		} else if (s_focus == 1U) {
			play_alarm_voice_test();
		} else if (s_focus == 2U) {
			add_alarm();
		} else if (s_focus == 3U) {
			delete_last_alarm();
		} else {
			s_alarm_selected = (uint8_t)(s_focus - 4U);
			if (s_alarm_selected < s_alarm_count) {
				s_view = UI_VIEW_ALARM_ITEM;
				s_focus = 0;
			}
		}
		return;
	}

	if (s_view == UI_VIEW_ALARM_ITEM) {
		ui_alarm_item_t *alarm = &s_alarms[s_alarm_selected];
		if (s_focus == 0U) {
			alarm->minute = (uint8_t)((alarm->minute + 5U) % 60U);
			if (alarm->minute == 0U) {
				alarm->hour = (uint8_t)((alarm->hour + 1U) % 24U);
			}
		} else if (s_focus == 1U) {
			alarm->repeat = !alarm->repeat;
		} else if (s_focus == 2U) {
			alarm->voice = !alarm->voice;
		} else {
			alarm->enabled = !alarm->enabled;
		}
		save_settings();
		return;
	}

	if (s_view == UI_VIEW_TODO_SETTINGS) {
		app_todo_snapshot_t todo = { 0 };
		(void)net_service_get_todo_snapshot(&todo);
		if (s_focus == 0U) {
			int ret = net_service_request_todo_sync_now();
			ESP_LOGI(TAG, "todo sync requested ret=%d", ret);
		} else if (s_focus == 1U) {
			static const uint8_t values[] = { 1, 5, 10, 30 };
			for (size_t i = 0; i < sizeof(values); i++) {
				if (s_todo_refresh_min == values[i]) {
					s_todo_refresh_min = values[(i + 1U) % sizeof(values)];
					save_settings();
					return;
				}
			}
			s_todo_refresh_min = 5;
			save_settings();
		} else if (s_focus == 2U) {
			s_todo_voice_on = !s_todo_voice_on;
			save_settings();
		} else if (s_focus == 3U) {
			play_todo_voice_test();
		} else {
			uint8_t index = (uint8_t)(s_focus - 4U);
			if (index < todo.count && index < APP_TODO_MAX_ITEMS) {
				mark_todo_done(&todo.items[index]);
			}
		}
		return;
	}

	if (s_view == UI_VIEW_ENV_SETTINGS) {
		if (s_focus == 0U) {
			s_env_voice_on = !s_env_voice_on;
			save_settings();
		} else if (s_focus == 1U) {
			play_env_voice_test();
		} else if (s_focus == 2U) {
			static const uint8_t values[] = { 5, 10, 30 };
			for (size_t i = 0; i < sizeof(values); i++) {
				if (s_env_sample_s == values[i]) {
					s_env_sample_s = values[(i + 1U) % sizeof(values)];
					(void)environment_service_set_sample_interval_s(s_env_sample_s);
					save_settings();
					return;
				}
			}
			s_env_sample_s = 10;
			(void)environment_service_set_sample_interval_s(s_env_sample_s);
			save_settings();
		} else if (s_focus == 3U) {
			static const int16_t values[] = { 0, 5, 10, 15, 20 };
			for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
				if (s_env_temp_low_c == values[i]) {
					s_env_temp_low_c = values[(i + 1U) % (sizeof(values) / sizeof(values[0]))];
					if (s_env_temp_low_c >= s_env_temp_high_c) {
						s_env_temp_high_c = (int16_t)(s_env_temp_low_c + 5);
					}
					save_settings();
					return;
				}
			}
			s_env_temp_low_c = 10;
			save_settings();
		} else if (s_focus == 4U) {
			static const int16_t values[] = { 25, 30, 35, 40, 45 };
			for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
				if (s_env_temp_high_c == values[i]) {
					s_env_temp_high_c = values[(i + 1U) % (sizeof(values) / sizeof(values[0]))];
					if (s_env_temp_high_c <= s_env_temp_low_c) {
						s_env_temp_low_c = (int16_t)(s_env_temp_high_c - 5);
					}
					save_settings();
					return;
				}
			}
			s_env_temp_high_c = 35;
			save_settings();
		} else if (s_focus == 5U) {
			static const uint16_t values[] = { 20, 30, 40, 50 };
			for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
				if (s_env_humi_low_percent == values[i]) {
					s_env_humi_low_percent = values[(i + 1U) % (sizeof(values) / sizeof(values[0]))];
					if (s_env_humi_low_percent >= s_env_humi_high_percent) {
						s_env_humi_high_percent = (uint16_t)(s_env_humi_low_percent + 10U);
					}
					save_settings();
					return;
				}
			}
			s_env_humi_low_percent = 30;
			save_settings();
		} else if (s_focus == 6U) {
			static const uint16_t values[] = { 60, 70, 80, 90 };
			for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
				if (s_env_humi_high_percent == values[i]) {
					s_env_humi_high_percent = values[(i + 1U) % (sizeof(values) / sizeof(values[0]))];
					if (s_env_humi_high_percent <= s_env_humi_low_percent) {
						s_env_humi_low_percent = (uint16_t)(s_env_humi_high_percent - 10U);
					}
					save_settings();
					return;
				}
			}
			s_env_humi_high_percent = 80;
			save_settings();
		} else if (s_focus == 7U) {
			static const uint16_t values[] = { 0, 10, 20, 50, 100 };
			for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
				if (s_env_lux_low == values[i]) {
					s_env_lux_low = values[(i + 1U) % (sizeof(values) / sizeof(values[0]))];
					if (s_env_lux_low >= s_env_lux_high) {
						s_env_lux_high = (uint16_t)(s_env_lux_low + 100U);
					}
					save_settings();
					return;
				}
			}
			s_env_lux_low = 20;
			save_settings();
		} else if (s_focus == 8U) {
			static const uint16_t values[] = { 300, 500, 800, 1000, 1500 };
			for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
				if (s_env_lux_high == values[i]) {
					s_env_lux_high = values[(i + 1U) % (sizeof(values) / sizeof(values[0]))];
					if (s_env_lux_high <= s_env_lux_low) {
						s_env_lux_low = s_env_lux_high > 100U ? (uint16_t)(s_env_lux_high - 100U) : 0U;
					}
					save_settings();
					return;
				}
			}
			s_env_lux_high = 1000;
			save_settings();
		} else {
			s_env_alert_on = !s_env_alert_on;
			save_settings();
		}
		return;
	}

	if (s_view == UI_VIEW_LOW_SETTINGS) {
		if (s_focus == 0U) {
			s_low_use_24h = !s_low_use_24h;
		} else if (s_focus == 1U) {
			static const uint16_t values[] = { 10, 30, 60, 120, 300 };
			for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
				if (s_low_enter_absent_s == values[i]) {
					s_low_enter_absent_s = values[(i + 1U) % (sizeof(values) / sizeof(values[0]))];
					save_settings();
					return;
				}
			}
			s_low_enter_absent_s = 60;
		} else {
			static const uint16_t values[] = { 1, 3, 5, 10, 30 };
			for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
				if (s_low_exit_present_s == values[i]) {
					s_low_exit_present_s = values[(i + 1U) % (sizeof(values) / sizeof(values[0]))];
					save_settings();
					return;
				}
			}
			s_low_exit_present_s = 3;
		}
		save_settings();
	}
}

static void process_key(size_t key_index)
{
	if (key_index >= UI_KEY_COUNT) {
		return;
	}

	if (s_view == UI_VIEW_MAIN) {
		if (key_index == UI_KEY_HOME_BACK) {
			if (s_main_page == UI_PAGE_ALARM) {
				if (s_alarm_count > 0U) {
					s_alarm_page_focus = (uint8_t)((s_alarm_page_focus + 1U) % s_alarm_count);
				}
			} else if (s_main_page == UI_PAGE_TODO) {
				app_todo_snapshot_t todo = { 0 };
				(void)net_service_get_todo_snapshot(&todo);
				const uint8_t count = todo.count < APP_TODO_MAX_ITEMS ? todo.count : APP_TODO_MAX_ITEMS;
				if (count > 0U) {
					s_todo_page_focus = (uint8_t)((s_todo_page_focus + 1U) % count);
				}
			} else {
				s_main_page = UI_PAGE_HOME;
				s_low_clock_auto_entered = false;
			}
		} else if (key_index == UI_KEY_PREV_UP) {
			s_main_page = (ui_main_page_t)((s_main_page + UI_MAIN_PAGE_COUNT - 1U) % UI_MAIN_PAGE_COUNT);
			s_low_clock_auto_entered = false;
		} else if (key_index == UI_KEY_NEXT_DOWN) {
			s_main_page = (ui_main_page_t)((s_main_page + 1U) % UI_MAIN_PAGE_COUNT);
			s_low_clock_auto_entered = false;
		} else {
			enter_settings_for_page();
		}
		s_dirty = true;
		return;
	}

	if (key_index == UI_KEY_HOME_BACK) {
		if (s_view == UI_VIEW_ALARM_ITEM) {
			s_view = UI_VIEW_ALARM_SETTINGS;
		} else {
			s_view = UI_VIEW_MAIN;
		}
		s_focus = 0;
		s_dirty = true;
		return;
	}

	const uint8_t count = focus_count_for_view();
	if (key_index == UI_KEY_PREV_UP) {
		s_focus = (uint8_t)((s_focus + count - 1U) % count);
	} else if (key_index == UI_KEY_NEXT_DOWN) {
		s_focus = (uint8_t)((s_focus + 1U) % count);
	} else {
		process_ok_in_settings();
	}
	s_dirty = true;
}

static void ui_task(void *arg)
{
	(void)arg;

	for (;;) {
		size_t key = 0;
		while (s_key_queue != NULL && xQueueReceive(s_key_queue, &key, 0) == pdTRUE) {
			process_key(key);
		}

		time_t now = 0;
		struct tm t = { 0 };
		time(&now);
		localtime_r(&now, &t);
		if (t.tm_sec != s_last_render_second) {
			s_last_render_second = t.tm_sec;
			sync_settings_from_model_for_main();
			update_low_clock_presence_runtime();
			s_dirty = true;
		}

		if (s_dirty && display_service_is_ready() && display_service_lock(100)) {
			render_ui();
			display_service_unlock();
			s_dirty = false;
		}

		vTaskDelay(pdMS_TO_TICKS(100));
	}
}

int ui_model_init(void)
{
	s_key_queue = xQueueCreate(12, sizeof(size_t));
	if (s_key_queue == NULL) {
		ESP_LOGE(TAG, "failed to create key queue");
		return -1;
	}

	app_settings_t settings = { 0 };
	settings_model_get(&settings);
	apply_settings(&settings);

	ESP_LOGI(TAG, "init");
	return 0;
}

int ui_model_start(void)
{
	BaseType_t ok = xTaskCreate(ui_task, "ui_task", 8192, NULL, 6, &s_ui_task);
	if (ok != pdPASS) {
		ESP_LOGE(TAG, "failed to create ui task");
		return -1;
	}

	return 0;
}

int ui_model_stop(void)
{
	if (s_ui_task != NULL) {
		vTaskDelete(s_ui_task);
		s_ui_task = NULL;
	}

	if (s_key_queue != NULL) {
		vQueueDelete(s_key_queue);
		s_key_queue = NULL;
	}

	return 0;
}

void ui_model_handle_key_press(size_t key_index)
{
	if (s_key_queue == NULL) {
		return;
	}

	if (xQueueSend(s_key_queue, &key_index, 0) != pdTRUE) {
		ESP_LOGW(TAG, "key queue full key=%u", (unsigned)(key_index + 1U));
	}
}
