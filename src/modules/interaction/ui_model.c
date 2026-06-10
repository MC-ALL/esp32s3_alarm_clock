#include "app_module.h"
#include <app_bus.h>
#include <display_service.h>
#include <module_common.h>
#include <presence_service.h>
#include <settings_model.h>
#include <sync_service.h>
#include <ui_layout.h>
#include <ui_model.h>
#include <ui_pages.h>
#include <ui_settings_pages.h>
#include <ui_style.h>
#include <ui_todo_view.h>
#include <ui_types.h>

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

static void publish_settings_event(app_bus_event_type_t type, const app_settings_t *settings);

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
	UI_VIEW_TODO_ITEM,
	UI_VIEW_TODO_DELETE_CONFIRM,
	UI_VIEW_ENV_SETTINGS,
	UI_VIEW_LOW_SETTINGS,
} ui_view_t;

typedef enum {
	UI_KEY_HOME_BACK = 0,
	UI_KEY_PREV_UP,
	UI_KEY_NEXT_DOWN,
	UI_KEY_OK,
} ui_key_t;

static QueueHandle_t s_key_queue;
static TaskHandle_t s_ui_task;
static ui_main_page_t s_main_page = UI_PAGE_HOME;
static ui_view_t s_view = UI_VIEW_MAIN;
static uint8_t s_focus;
static uint8_t s_alarm_selected;
static uint8_t s_alarm_page_focus;
static uint8_t s_todo_page_focus;
static uint8_t s_todo_selected;
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
static bool s_dirty = true;
static int s_last_render_second = -1;
static int64_t s_presence_absent_since_us;
static int64_t s_presence_present_since_us;

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
		return;
	}
	publish_settings_event(APP_BUS_EVENT_SETTINGS_CHANGED, &settings);
}

static void publish_settings_event(app_bus_event_type_t type, const app_settings_t *settings)
{
	if (settings == NULL) {
		return;
	}

	app_bus_event_t event = {
		.type = type,
	};
	event.data.settings.settings = *settings;
	if (app_bus_publish(&event) != 0) {
		ESP_LOGW(TAG, "publish settings event failed type=%d", (int)type);
	}
}

static void save_alarm_settings(void)
{
	app_settings_t settings = { 0 };
	collect_settings(&settings);
	int ret = settings_model_set(&settings);
	if (ret != 0) {
		ESP_LOGW(TAG, "alarm settings save failed ret=%d", ret);
		return;
	}
	publish_settings_event(APP_BUS_EVENT_SETTINGS_CHANGED, &settings);
	publish_settings_event(APP_BUS_EVENT_ALARM_SETTINGS_CHANGED, &settings);
}

static void save_voice_settings(void)
{
	app_settings_t settings = { 0 };
	collect_settings(&settings);
	int ret = settings_model_set(&settings);
	if (ret != 0) {
		ESP_LOGW(TAG, "voice settings save failed ret=%d", ret);
		return;
	}
	publish_settings_event(APP_BUS_EVENT_SETTINGS_CHANGED, &settings);
	publish_settings_event(APP_BUS_EVENT_VOICE_SETTINGS_CHANGED, &settings);
}

static int publish_todo_action(app_bus_event_type_t type, const char *todo_id)
{
	if (todo_id == NULL || todo_id[0] == '\0') {
		return -1;
	}
	app_bus_event_t event = {
		.type = type,
	};
	strlcpy(event.data.todo.todo_id, todo_id, sizeof(event.data.todo.todo_id));
	return app_bus_publish(&event);
}

static int publish_device_event(const char *event_type, const char *todo_id)
{
	if (event_type == NULL || event_type[0] == '\0') {
		return -1;
	}
	app_bus_event_t event = {
		.type = APP_BUS_EVENT_DEVICE_EVENT,
	};
	strlcpy(event.data.device_event.event_type, event_type, sizeof(event.data.device_event.event_type));
	if (todo_id != NULL) {
		strlcpy(event.data.device_event.todo_id, todo_id, sizeof(event.data.device_event.todo_id));
	}
	return app_bus_publish(&event);
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

static const char *ok_icon_for_view(void)
{
	if (s_view == UI_VIEW_MAIN) {
		if (s_main_page == UI_PAGE_TODO) {
			app_todo_snapshot_t todo = { 0 };
			(void)sync_service_get_todo_snapshot(&todo);
			return s_todo_page_focus < ui_todo_item_count(&todo) ? LV_SYMBOL_OK : LV_SYMBOL_SETTINGS;
		}
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
		ui_box(screen, (int32_t)(i * UI_KEY_W), UI_KEYBAR_Y, UI_KEY_W, UI_KEYBAR_H, ui_color_black());
		ui_label(screen, icons[i], UI_FONT_32, ui_color_white(), (int32_t)(i * UI_KEY_W), UI_KEYBAR_Y + 22,
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
	ui_obj_plain(screen, ui_color_black());
	lv_obj_set_size(screen, UI_SCREEN_W, UI_SCREEN_H);

	if (s_view == UI_VIEW_MAIN) {
		ui_pages_state_t pages = {
			.home_clock_only = s_home_clock_only,
			.use_24h = s_use_24h,
			.low_use_24h = s_low_use_24h,
			.env_alert_on = s_env_alert_on,
			.env_temp_low_c = s_env_temp_low_c,
			.env_temp_high_c = s_env_temp_high_c,
			.env_humi_low_percent = s_env_humi_low_percent,
			.env_humi_high_percent = s_env_humi_high_percent,
			.env_lux_low = s_env_lux_low,
			.env_lux_high = s_env_lux_high,
			.alarms = s_alarms,
			.alarm_count = s_alarm_count,
			.alarm_page_focus = &s_alarm_page_focus,
			.todo_page_focus = &s_todo_page_focus,
		};
		switch (s_main_page) {
		case UI_PAGE_HOME:
			ui_pages_render_home(screen, &pages);
			break;
		case UI_PAGE_ALARM:
			ui_pages_render_alarm(screen, &pages);
			break;
		case UI_PAGE_TODO:
			ui_pages_render_todo(screen, &pages);
			break;
		case UI_PAGE_ENV:
			ui_pages_render_env(screen, &pages);
			break;
		case UI_PAGE_WIFI:
			ui_pages_render_wifi(screen);
			break;
		case UI_PAGE_LOW_CLOCK:
			ui_pages_render_low_clock(screen, &pages);
			break;
		default:
			break;
		}
	} else {
		ui_settings_pages_state_t settings_pages = {
			.focus = s_focus,
			.home_clock_only = s_home_clock_only,
			.use_24h = s_use_24h,
			.low_use_24h = s_low_use_24h,
			.home_hour_chime_on = s_home_hour_chime_on,
			.alarm_voice_on = s_alarm_voice_on,
			.todo_voice_on = s_todo_voice_on,
			.env_voice_on = s_env_voice_on,
			.env_alert_on = s_env_alert_on,
			.env_sample_s = s_env_sample_s,
			.env_temp_low_c = s_env_temp_low_c,
			.env_temp_high_c = s_env_temp_high_c,
			.env_humi_low_percent = s_env_humi_low_percent,
			.env_humi_high_percent = s_env_humi_high_percent,
			.env_lux_low = s_env_lux_low,
			.env_lux_high = s_env_lux_high,
			.low_enter_absent_s = s_low_enter_absent_s,
			.low_exit_present_s = s_low_exit_present_s,
			.alarms = s_alarms,
			.alarm_count = s_alarm_count,
			.alarm_selected = s_alarm_selected,
			.todo_selected = s_todo_selected,
		};
		if (s_view == UI_VIEW_HOME_SETTINGS) {
			ui_settings_pages_render_home(screen, &settings_pages);
		} else if (s_view == UI_VIEW_ALARM_SETTINGS) {
			ui_settings_pages_render_alarm(screen, &settings_pages);
		} else if (s_view == UI_VIEW_ALARM_ITEM) {
			ui_settings_pages_render_alarm_item(screen, &settings_pages);
		} else if (s_view == UI_VIEW_TODO_SETTINGS) {
			ui_settings_pages_render_todo(screen, &settings_pages);
		} else if (s_view == UI_VIEW_TODO_ITEM) {
			ui_settings_pages_render_todo_item(screen, &settings_pages);
		} else if (s_view == UI_VIEW_TODO_DELETE_CONFIRM) {
			ui_settings_pages_render_todo_delete_confirm(screen, &settings_pages);
		} else if (s_view == UI_VIEW_ENV_SETTINGS) {
			ui_settings_pages_render_env(screen, &settings_pages);
		} else if (s_view == UI_VIEW_LOW_SETTINGS) {
			ui_settings_pages_render_low(screen, &settings_pages);
		}
	}

	render_keybar(screen);
}

static uint8_t focus_count_for_view(void)
{
	switch (s_view) {
	case UI_VIEW_HOME_SETTINGS:
		return 3;
	case UI_VIEW_ALARM_SETTINGS:
		return (uint8_t)(4U + s_alarm_count);
	case UI_VIEW_ALARM_ITEM:
		return 4;
	case UI_VIEW_TODO_SETTINGS:
		return 4;
	case UI_VIEW_TODO_ITEM:
		return 2;
	case UI_VIEW_TODO_DELETE_CONFIRM:
		return 2;
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
		app_bus_event_t event = {
			.type = APP_BUS_EVENT_AUDIO_PLAY_REQUEST,
		};
		event.data.audio.event_id = events[i];
		int ret = app_bus_publish(&event);
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
	const app_audio_event_t events[] = { APP_AUDIO_EVENT_TODO_SYNC_UP };
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
				(void)publish_device_event("welcome_played", NULL);
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
	save_alarm_settings();
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
	save_alarm_settings();
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
			save_voice_settings();
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
		save_alarm_settings();
		return;
	}

	if (s_view == UI_VIEW_TODO_SETTINGS) {
		if (s_focus == 0U) {
			app_bus_event_t event = {
				.type = APP_BUS_EVENT_TODO_SYNC_REQUEST,
			};
			int ret = app_bus_publish(&event);
			ESP_LOGI(TAG, "todo sync requested ret=%d", ret);
		} else if (s_focus == 1U) {
			return;
		} else if (s_focus == 2U) {
			s_todo_voice_on = !s_todo_voice_on;
			save_voice_settings();
		} else if (s_focus == 3U) {
			play_todo_voice_test();
		}
		return;
	}

	if (s_view == UI_VIEW_TODO_ITEM) {
		app_todo_snapshot_t todo = { 0 };
		(void)sync_service_get_todo_snapshot(&todo);
		const app_todo_item_t *item = ui_todo_item_at(&todo, s_todo_selected);
		if (item == NULL) {
			s_view = UI_VIEW_MAIN;
			return;
		}

		if (s_focus == 0U) {
			int ret = publish_todo_action(APP_BUS_EVENT_TODO_COMPLETE_REQUEST, item->id);
			ESP_LOGI(TAG, "todo toggle requested id=%s done=%d ret=%d", item->id, item->done ? 0 : 1, ret);
			s_view = UI_VIEW_MAIN;
			s_focus = 0;
		} else {
			s_view = UI_VIEW_TODO_DELETE_CONFIRM;
			s_focus = 0;
		}
		return;
	}

	if (s_view == UI_VIEW_TODO_DELETE_CONFIRM) {
		app_todo_snapshot_t todo = { 0 };
		(void)sync_service_get_todo_snapshot(&todo);
		const app_todo_item_t *item = ui_todo_item_at(&todo, s_todo_selected);
		if (s_focus == 0U && item != NULL) {
			int ret = publish_todo_action(APP_BUS_EVENT_TODO_DELETE_REQUEST, item->id);
			ESP_LOGI(TAG, "todo delete requested id=%s ret=%d", item->id, ret);
			s_view = UI_VIEW_MAIN;
			s_focus = 0;
		} else {
			s_view = UI_VIEW_TODO_ITEM;
			s_focus = 0;
		}
		return;
	}

	if (s_view == UI_VIEW_ENV_SETTINGS) {
		if (s_focus == 0U) {
			s_env_voice_on = !s_env_voice_on;
			save_voice_settings();
		} else if (s_focus == 1U) {
			play_env_voice_test();
		} else if (s_focus == 2U) {
			static const uint8_t values[] = { 5, 10, 30 };
			for (size_t i = 0; i < sizeof(values); i++) {
				if (s_env_sample_s == values[i]) {
					s_env_sample_s = values[(i + 1U) % sizeof(values)];
					save_settings();
					return;
				}
			}
			s_env_sample_s = 10;
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
			save_voice_settings();
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
				(void)sync_service_get_todo_snapshot(&todo);
				const uint8_t count = (uint8_t)(ui_todo_item_count(&todo) + 1U);
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
			if (s_main_page == UI_PAGE_TODO) {
				app_todo_snapshot_t todo = { 0 };
				(void)sync_service_get_todo_snapshot(&todo);
				const uint8_t item_count = ui_todo_item_count(&todo);
				if (s_todo_page_focus < item_count) {
					s_todo_selected = s_todo_page_focus;
					s_view = UI_VIEW_TODO_ITEM;
					s_focus = 0;
				} else {
					s_view = UI_VIEW_TODO_SETTINGS;
					s_focus = 0;
				}
			} else {
				enter_settings_for_page();
			}
		}
		s_dirty = true;
		return;
	}

	if (key_index == UI_KEY_HOME_BACK) {
		if (s_view == UI_VIEW_ALARM_ITEM) {
			s_view = UI_VIEW_ALARM_SETTINGS;
		} else if (s_view == UI_VIEW_TODO_DELETE_CONFIRM) {
			s_view = UI_VIEW_TODO_ITEM;
		} else if (s_view == UI_VIEW_TODO_ITEM || s_view == UI_VIEW_TODO_SETTINGS) {
			s_view = UI_VIEW_MAIN;
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

static void ui_model_handle_key_press(size_t key_index)
{
	if (s_key_queue == NULL) {
		return;
	}

	if (xQueueSend(s_key_queue, &key_index, 0) != pdTRUE) {
		ESP_LOGW(TAG, "key queue full key=%u", (unsigned)(key_index + 1U));
	}
}

static void ui_bus_handler(const app_bus_event_t *event, void *ctx)
{
	(void)ctx;
	if (event == NULL || event->type != APP_BUS_EVENT_INPUT_KEY_PRESSED) {
		return;
	}
	ui_model_handle_key_press((size_t)event->data.input.key_index);
}

int ui_model_init(void)
{
	s_key_queue = xQueueCreate(12, sizeof(size_t));
	if (s_key_queue == NULL) {
		ESP_LOGE(TAG, "failed to create key queue");
		return -1;
	}
	(void)app_bus_subscribe(APP_BUS_EVENT_INPUT_KEY_PRESSED, ui_bus_handler, NULL);

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
