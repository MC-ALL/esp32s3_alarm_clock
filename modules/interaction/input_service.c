#include "app_module.h"
#include <app/display_service.h>
#include <app/hw_config.h>
#include <app/input_service.h>
#include <app/module_common.h>

#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

static const char *TAG = "input";
static QueueHandle_t s_gpio_evt_queue;
static TaskHandle_t s_input_task;
static bool s_key_levels[4] = { true, true, true, true };
static bool s_key_pressed_latched[4];
static int64_t s_last_edge_us[4];
static bool s_isr_service_owned;

static const gpio_num_t KEY_PINS[4] = {
	APP_PIN_KEY1,
	APP_PIN_KEY2,
	APP_PIN_KEY3,
	APP_PIN_KEY4,
};

static int key_index_from_gpio(gpio_num_t gpio_num)
{
	for (size_t i = 0; i < 4; i++) {
		if (KEY_PINS[i] == gpio_num) {
			return (int)i;
		}
	}

	return -1;
}

static void IRAM_ATTR input_gpio_isr(void *arg)
{
	const uint32_t gpio_num = (uint32_t)(uintptr_t)arg;
	BaseType_t task_woken = pdFALSE;

	if (s_gpio_evt_queue != NULL) {
		xQueueSendFromISR(s_gpio_evt_queue, &gpio_num, &task_woken);
	}

	if (task_woken == pdTRUE) {
		portYIELD_FROM_ISR();
	}
}

static void input_task(void *arg)
{
	static const TickType_t DEBOUNCE_TICKS = pdMS_TO_TICKS(30);
	(void)arg;

	for (;;) {
		uint32_t gpio_num = 0;
		if (xQueueReceive(s_gpio_evt_queue, &gpio_num, portMAX_DELAY) != pdTRUE) {
			continue;
		}

		const int key_index = key_index_from_gpio((gpio_num_t)gpio_num);
		if (key_index < 0) {
			continue;
		}

		const int64_t now_us = esp_timer_get_time();
		if ((now_us - s_last_edge_us[key_index]) < 10000) {
			continue;
		}

		s_last_edge_us[key_index] = now_us;
		vTaskDelay(DEBOUNCE_TICKS);

		const bool released = gpio_get_level((gpio_num_t)gpio_num) != 0;
		s_key_levels[key_index] = released;

		if (!released) {
			if (s_key_pressed_latched[key_index]) {
				continue;
			}

			s_key_pressed_latched[key_index] = true;
			ESP_LOGI(TAG, "key%u pressed", (unsigned)(key_index + 1));
			display_service_handle_key_press((size_t)key_index);
		} else if (s_key_pressed_latched[key_index]) {
			s_key_pressed_latched[key_index] = false;
			ESP_LOGI(TAG, "key%u released", (unsigned)(key_index + 1));
		}
	}
}

int input_service_init(void)
{
	gpio_config_t io_conf = {
		.intr_type = GPIO_INTR_ANYEDGE,
		.mode = GPIO_MODE_INPUT,
		.pin_bit_mask =
			(1ULL << APP_PIN_KEY1) |
			(1ULL << APP_PIN_KEY2) |
			(1ULL << APP_PIN_KEY3) |
			(1ULL << APP_PIN_KEY4),
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.pull_up_en = GPIO_PULLUP_ENABLE,
	};

	esp_err_t err = gpio_config(&io_conf);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	s_gpio_evt_queue = xQueueCreate(16, sizeof(uint32_t));
	if (s_gpio_evt_queue == NULL) {
		ESP_LOGE(TAG, "failed to create input queue");
		return -1;
	}

	err = gpio_install_isr_service(0);
	if (err == ESP_OK) {
		s_isr_service_owned = true;
	} else if (err != ESP_ERR_INVALID_STATE) {
		ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(err));
		vQueueDelete(s_gpio_evt_queue);
		s_gpio_evt_queue = NULL;
		return (int)err;
	}

	for (size_t i = 0; i < 4; i++) {
		s_key_levels[i] = gpio_get_level(KEY_PINS[i]) != 0;
		s_key_pressed_latched[i] = !s_key_levels[i];
		s_last_edge_us[i] = 0;
		err = gpio_isr_handler_add(KEY_PINS[i], input_gpio_isr, (void *)(uintptr_t)KEY_PINS[i]);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "gpio_isr_handler_add failed on GPIO%d: %s",
				KEY_PINS[i], esp_err_to_name(err));
			for (size_t j = 0; j < i; j++) {
				(void)gpio_isr_handler_remove(KEY_PINS[j]);
			}
			if (s_isr_service_owned) {
				(void)gpio_uninstall_isr_service();
				s_isr_service_owned = false;
			}
			vQueueDelete(s_gpio_evt_queue);
			s_gpio_evt_queue = NULL;
			return (int)err;
		}
	}

	ESP_LOGI(TAG, "init on GPIOs %d,%d,%d,%d",
		APP_PIN_KEY1, APP_PIN_KEY2, APP_PIN_KEY3, APP_PIN_KEY4);
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
	for (size_t i = 0; i < 4; i++) {
		(void)gpio_isr_handler_remove(KEY_PINS[i]);
	}
	if (s_isr_service_owned) {
		(void)gpio_uninstall_isr_service();
		s_isr_service_owned = false;
	}

	if (s_input_task != NULL) {
		vTaskDelete(s_input_task);
		s_input_task = NULL;
	}

	if (s_gpio_evt_queue != NULL) {
		vQueueDelete(s_gpio_evt_queue);
		s_gpio_evt_queue = NULL;
	}

	return 0;
}

bool input_service_get_key_level(size_t key_index)
{
	if (key_index >= 4) {
		return true;
	}

	return s_key_levels[key_index];
}
