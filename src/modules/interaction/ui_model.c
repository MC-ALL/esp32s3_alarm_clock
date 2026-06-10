#include "app_module.h"
#include <display_service.h>
#include <module_common.h>
#include <ui_actions.h>
#include <ui_key_queue.h>
#include <ui_low_clock_runtime.h>
#include <ui_model.h>
#include <ui_navigation.h>
#include <ui_renderer.h>
#include <ui_settings_controller.h>
#include <ui_settings_store.h>
#include <ui_types.h>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <inttypes.h>
#include <time.h>

static const char *TAG = "ui";

static TaskHandle_t s_ui_task;
static ui_main_page_t s_main_page = UI_PAGE_HOME;
static ui_view_t s_view = UI_VIEW_MAIN;
static uint8_t s_focus;
static uint8_t s_alarm_selected;
static uint8_t s_alarm_page_focus;
static uint8_t s_todo_page_focus;
static uint8_t s_todo_selected;
static ui_low_clock_runtime_t s_low_clock_runtime;
static ui_settings_store_state_t s_settings;
static bool s_dirty = true;
static int s_last_render_second = -1;

static void save_settings(void)
{
	(void)ui_settings_store_save(&s_settings, false, false);
}

static void save_alarm_settings(void)
{
	(void)ui_settings_store_save(&s_settings, true, false);
}

static void save_voice_settings(void)
{
	(void)ui_settings_store_save(&s_settings, false, true);
}

static void sync_settings_from_model_for_main(void)
{
	if (s_view != UI_VIEW_MAIN) {
		return;
	}

	ui_settings_store_load(&s_settings);
}

static void render_ui(void)
{
	ui_renderer_state_t state = {
		.main_page = s_main_page,
		.view = s_view,
		.focus = s_focus,
		.alarm_selected = s_alarm_selected,
		.todo_selected = s_todo_selected,
		.home_clock_only = s_settings.home_clock_only,
		.use_24h = s_settings.use_24h,
		.low_use_24h = s_settings.low_use_24h,
		.home_hour_chime_on = s_settings.home_hour_chime_on,
		.alarm_voice_on = s_settings.alarm_voice_on,
		.todo_voice_on = s_settings.todo_voice_on,
		.env_voice_on = s_settings.env_voice_on,
		.env_alert_on = s_settings.env_alert_on,
		.env_sample_s = s_settings.env_sample_s,
		.env_temp_low_c = s_settings.env_temp_low_c,
		.env_temp_high_c = s_settings.env_temp_high_c,
		.env_humi_low_percent = s_settings.env_humi_low_percent,
		.env_humi_high_percent = s_settings.env_humi_high_percent,
		.env_lux_low = s_settings.env_lux_low,
		.env_lux_high = s_settings.env_lux_high,
		.low_enter_absent_s = s_settings.low_enter_absent_s,
		.low_exit_present_s = s_settings.low_exit_present_s,
		.alarms = s_settings.alarms,
		.alarm_count = s_settings.alarm_count,
		.alarm_page_focus = &s_alarm_page_focus,
		.todo_page_focus = &s_todo_page_focus,
	};
	ui_renderer_render(&state);
}

static void update_low_clock_presence_runtime(void)
{
	ui_low_clock_result_t result = { 0 };
	ui_low_clock_action_t action =
		ui_low_clock_runtime_update(&s_low_clock_runtime, &s_main_page, s_view,
					    s_settings.low_enter_absent_s,
					    s_settings.low_exit_present_s, &result);
	if (action == UI_LOW_CLOCK_ACTION_ENTER) {
		s_dirty = true;
		ESP_LOGI(TAG, "enter low clock absent_s=%" PRIi64 " threshold=%u healthy=%d fallback=%d",
			 result.duration_s,
			 (unsigned)result.threshold_s,
			 result.presence.radar_healthy ? 1 : 0,
			 result.presence.using_out_fallback ? 1 : 0);
		return;
	}

	if (action == UI_LOW_CLOCK_ACTION_EXIT) {
		s_dirty = true;
		(void)ui_actions_publish_device_event("welcome_played", NULL);
		ESP_LOGI(TAG, "exit low clock present_s=%" PRIi64 " threshold=%u healthy=%d fallback=%d",
			 result.duration_s,
			 (unsigned)result.threshold_s,
			 result.presence.radar_healthy ? 1 : 0,
			 result.presence.using_out_fallback ? 1 : 0);
	}
}

static void process_ok_in_settings(void)
{
	ui_settings_controller_state_t settings = {
		.view = &s_view,
		.focus = &s_focus,
		.alarm_selected = &s_alarm_selected,
		.todo_selected = &s_todo_selected,
		.home_clock_only = &s_settings.home_clock_only,
		.use_24h = &s_settings.use_24h,
		.low_use_24h = &s_settings.low_use_24h,
		.home_hour_chime_on = &s_settings.home_hour_chime_on,
		.alarm_voice_on = &s_settings.alarm_voice_on,
		.todo_voice_on = &s_settings.todo_voice_on,
		.env_voice_on = &s_settings.env_voice_on,
		.env_alert_on = &s_settings.env_alert_on,
		.env_sample_s = &s_settings.env_sample_s,
		.env_temp_low_c = &s_settings.env_temp_low_c,
		.env_temp_high_c = &s_settings.env_temp_high_c,
		.env_humi_low_percent = &s_settings.env_humi_low_percent,
		.env_humi_high_percent = &s_settings.env_humi_high_percent,
		.env_lux_low = &s_settings.env_lux_low,
		.env_lux_high = &s_settings.env_lux_high,
		.low_enter_absent_s = &s_settings.low_enter_absent_s,
		.low_exit_present_s = &s_settings.low_exit_present_s,
		.alarms = s_settings.alarms,
		.alarm_count = &s_settings.alarm_count,
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
		.alarm_count = s_settings.alarm_count,
		.low_clock_auto_entered = &s_low_clock_runtime.auto_entered,
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
		while (ui_key_queue_receive(&key)) {
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
	if (ui_key_queue_init() != 0) {
		return -1;
	}

	ui_settings_store_load(&s_settings);

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

	ui_key_queue_deinit();

	return 0;
}
