#include <display_lvgl_runtime.h>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>

static const char *TAG = "display_lvgl_runtime";

static TaskHandle_t s_lvgl_task;
static esp_timer_handle_t s_lvgl_tick_timer;
static display_lvgl_runtime_lock_fn_t s_lock_fn;
static display_lvgl_runtime_unlock_fn_t s_unlock_fn;

static void lvgl_tick_cb(void *arg)
{
	(void)arg;
	lv_tick_inc(2);
}

void display_lvgl_runtime_handle_timer(void)
{
	(void)lv_timer_handler();
}

static void display_lvgl_task(void *arg)
{
	(void)arg;

	for (;;) {
		if (s_lock_fn != NULL && s_unlock_fn != NULL && s_lock_fn(UINT32_MAX)) {
			display_lvgl_runtime_handle_timer();
			s_unlock_fn();
		}
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}

int display_lvgl_runtime_init(display_lvgl_runtime_lock_fn_t lock_fn,
			      display_lvgl_runtime_unlock_fn_t unlock_fn)
{
	s_lock_fn = lock_fn;
	s_unlock_fn = unlock_fn;

	const esp_timer_create_args_t tick_timer_args = {
		.callback = &lvgl_tick_cb,
		.name = "lvgl_tick",
	};
	esp_err_t err = esp_timer_create(&tick_timer_args, &s_lvgl_tick_timer);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_timer_create failed: %s", esp_err_to_name(err));
		display_lvgl_runtime_stop();
		return (int)err;
	}

	err = esp_timer_start_periodic(s_lvgl_tick_timer, 2000);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_timer_start_periodic failed: %s", esp_err_to_name(err));
		display_lvgl_runtime_stop();
		return (int)err;
	}

	return 0;
}

int display_lvgl_runtime_start(void)
{
	BaseType_t ok = xTaskCreate(display_lvgl_task, "display_task", 4096, NULL, 7, &s_lvgl_task);
	if (ok != pdPASS) {
		ESP_LOGE(TAG, "failed to create display task");
		return -1;
	}

	return 0;
}

void display_lvgl_runtime_stop(void)
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

	s_lock_fn = NULL;
	s_unlock_fn = NULL;
}
