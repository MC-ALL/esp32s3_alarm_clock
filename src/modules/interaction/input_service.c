#include "app_module.h"
#include <core/app_bus.h>
#include <interaction/input_gpio.h>
#include <interaction/input_service.h>
#include <core/module_common.h>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static const char *TAG = "input";
static TaskHandle_t s_input_task;
static bool s_key_levels[INPUT_GPIO_KEY_COUNT] = { true, true, true, true };
static bool s_key_pressed_latched[INPUT_GPIO_KEY_COUNT];
static int64_t s_last_edge_us[INPUT_GPIO_KEY_COUNT];

static void input_task(void *arg)
{
	static const TickType_t DEBOUNCE_TICKS = pdMS_TO_TICKS(30);
	(void)arg;

	for (;;) {
		uint32_t gpio_num = 0;
		if (input_gpio_wait_edge(&gpio_num) != 0) {
			continue;
		}

		const int key_index = input_gpio_key_index_from_gpio(gpio_num);
		if (key_index < 0) {
			continue;
		}

		const int64_t now_us = esp_timer_get_time();
		if ((now_us - s_last_edge_us[key_index]) < 10000) {
			continue;
		}

		s_last_edge_us[key_index] = now_us;
		vTaskDelay(DEBOUNCE_TICKS);

		const bool released = input_gpio_read_key_level((size_t)key_index);
		s_key_levels[key_index] = released;

		if (!released) {
			if (s_key_pressed_latched[key_index]) {
				continue;
			}

			s_key_pressed_latched[key_index] = true;
			ESP_LOGI(TAG, "key%u pressed", (unsigned)(key_index + 1));
			app_bus_event_t event = {
				.type = APP_BUS_EVENT_INPUT_KEY_PRESSED,
				.timestamp_us = now_us,
			};
			event.data.input.key_index = (uint8_t)key_index;
			(void)app_bus_publish(&event);
		} else if (s_key_pressed_latched[key_index]) {
			s_key_pressed_latched[key_index] = false;
			ESP_LOGI(TAG, "key%u released", (unsigned)(key_index + 1));
		}
	}
}

int input_service_init(void)
{
	int ret = input_gpio_init();
	if (ret != 0) {
		return ret;
	}

	for (size_t i = 0; i < INPUT_GPIO_KEY_COUNT; i++) {
		s_key_levels[i] = input_gpio_read_key_level(i);
		s_key_pressed_latched[i] = !s_key_levels[i];
		s_last_edge_us[i] = 0;
	}

	ESP_LOGI(TAG, "init");
	return 0;
}

int input_service_start(void)
{
	BaseType_t ok = xTaskCreate(input_task, "input_task", 3072, NULL, 10, &s_input_task);
	if (ok != pdPASS) {
		ESP_LOGE(TAG, "failed to create input task");
		return -1;
	}

	return 0;
}

int input_service_stop(void)
{
	if (s_input_task != NULL) {
		vTaskDelete(s_input_task);
		s_input_task = NULL;
	}

	input_gpio_deinit();

	return 0;
}

bool input_service_get_key_level(size_t key_index)
{
	if (key_index >= INPUT_GPIO_KEY_COUNT) {
		return true;
	}

	return s_key_levels[key_index];
}
