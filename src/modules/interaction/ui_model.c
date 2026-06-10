#include "app_module.h"
#include <app_bus.h>
#include <display_service.h>
#include <module_common.h>
#include <presence_service.h>
#include <settings_model.h>
#include <ui_actions.h>
#include <ui_model.h>
#include <ui_navigation.h>
#include <ui_renderer.h>
#include <ui_settings_controller.h>
#include <ui_types.h>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <inttypes.h>
#include <time.h>

static const char *TAG = "ui";

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
	ui_actions_publish_settings(APP_BUS_EVENT_SETTINGS_CHANGED, &settings);
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
	ui_actions_publish_settings(APP_BUS_EVENT_SETTINGS_CHANGED, &settings);
	ui_actions_publish_settings(APP_BUS_EVENT_ALARM_SETTINGS_CHANGED, &settings);
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
	ui_actions_publish_settings(APP_BUS_EVENT_SETTINGS_CHANGED, &settings);
	ui_actions_publish_settings(APP_BUS_EVENT_VOICE_SETTINGS_CHANGED, &settings);
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

static void render_ui(void)
{
	ui_renderer_state_t state = {
		.main_page = s_main_page,
		.view = s_view,
		.focus = s_focus,
		.alarm_selected = s_alarm_selected,
		.todo_selected = s_todo_selected,
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
		.alarm_page_focus = &s_alarm_page_focus,
		.todo_page_focus = &s_todo_page_focus,
	};
	ui_renderer_render(&state);
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
			(void)ui_actions_publish_device_event("welcome_played", NULL);
			ESP_LOGI(TAG, "exit low clock present_s=%" PRIi64 " threshold=%u healthy=%d fallback=%d",
				 present_s,
				 (unsigned)s_low_exit_present_s,
				 presence.radar_healthy ? 1 : 0,
				 presence.using_out_fallback ? 1 : 0);
		}
	}
}

static void process_ok_in_settings(void)
{
	ui_settings_controller_state_t settings = {
		.view = &s_view,
		.focus = &s_focus,
		.alarm_selected = &s_alarm_selected,
		.todo_selected = &s_todo_selected,
		.home_clock_only = &s_home_clock_only,
		.use_24h = &s_use_24h,
		.low_use_24h = &s_low_use_24h,
		.home_hour_chime_on = &s_home_hour_chime_on,
		.alarm_voice_on = &s_alarm_voice_on,
		.todo_voice_on = &s_todo_voice_on,
		.env_voice_on = &s_env_voice_on,
		.env_alert_on = &s_env_alert_on,
		.env_sample_s = &s_env_sample_s,
		.env_temp_low_c = &s_env_temp_low_c,
		.env_temp_high_c = &s_env_temp_high_c,
		.env_humi_low_percent = &s_env_humi_low_percent,
		.env_humi_high_percent = &s_env_humi_high_percent,
		.env_lux_low = &s_env_lux_low,
		.env_lux_high = &s_env_lux_high,
		.low_enter_absent_s = &s_low_enter_absent_s,
		.low_exit_present_s = &s_low_exit_present_s,
		.alarms = s_alarms,
		.alarm_count = &s_alarm_count,
		.save_settings = save_settings,
		.save_alarm_settings = save_alarm_settings,
		.save_voice_settings = save_voice_settings,
	};
	ui_settings_controller_process_ok(&settings);
}

static void process_settings_ok_action(void *ctx)
{
	(void)ctx;
	process_ok_in_settings();
}

static void navigation_log_info(const char *message, void *ctx)
{
	(void)ctx;
	ESP_LOGI(TAG, "%s", message);
}

static void process_key(size_t key_index)
{
	ui_navigation_state_t navigation = {
		.main_page = &s_main_page,
		.view = &s_view,
		.focus = &s_focus,
		.alarm_page_focus = &s_alarm_page_focus,
		.todo_page_focus = &s_todo_page_focus,
		.todo_selected = &s_todo_selected,
		.alarm_count = s_alarm_count,
		.low_clock_auto_entered = &s_low_clock_auto_entered,
		.process_settings_ok = process_settings_ok_action,
		.log_info = navigation_log_info,
	};
	if (ui_navigation_process_key(&navigation, key_index)) {
		s_dirty = true;
	}
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
