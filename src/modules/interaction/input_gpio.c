#include <input_gpio.h>

#include <hw_config.h>

#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdint.h>

static const char *TAG = "input_gpio";

static QueueHandle_t s_gpio_evt_queue;
static bool s_isr_service_owned;

static const gpio_num_t KEY_PINS[INPUT_GPIO_KEY_COUNT] = {
	APP_PIN_KEY1,
	APP_PIN_KEY2,
	APP_PIN_KEY3,
	APP_PIN_KEY4,
};

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

int input_gpio_key_index_from_gpio(uint32_t gpio_num)
{
	for (size_t i = 0; i < INPUT_GPIO_KEY_COUNT; i++) {
		if ((uint32_t)KEY_PINS[i] == gpio_num) {
			return (int)i;
		}
	}

	return -1;
}

bool input_gpio_read_key_level(size_t key_index)
{
	if (key_index >= INPUT_GPIO_KEY_COUNT) {
		return true;
	}

	return gpio_get_level(KEY_PINS[key_index]) != 0;
}

int input_gpio_wait_edge(uint32_t *gpio_num)
{
	if (s_gpio_evt_queue == NULL || gpio_num == NULL) {
		return -1;
	}

	return xQueueReceive(s_gpio_evt_queue, gpio_num, portMAX_DELAY) == pdTRUE ? 0 : -1;
}

int input_gpio_init(void)
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
		input_gpio_deinit();
		return (int)err;
	}

	for (size_t i = 0; i < INPUT_GPIO_KEY_COUNT; i++) {
		err = gpio_isr_handler_add(KEY_PINS[i], input_gpio_isr, (void *)(uintptr_t)KEY_PINS[i]);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "gpio_isr_handler_add failed on GPIO%d: %s",
				 KEY_PINS[i], esp_err_to_name(err));
			input_gpio_deinit();
			return (int)err;
		}
	}

	ESP_LOGI(TAG, "init on GPIOs %d,%d,%d,%d",
		 APP_PIN_KEY1, APP_PIN_KEY2, APP_PIN_KEY3, APP_PIN_KEY4);
	return 0;
}

void input_gpio_deinit(void)
{
	for (size_t i = 0; i < INPUT_GPIO_KEY_COUNT; i++) {
		(void)gpio_isr_handler_remove(KEY_PINS[i]);
	}
	if (s_isr_service_owned) {
		(void)gpio_uninstall_isr_service();
		s_isr_service_owned = false;
	}

	if (s_gpio_evt_queue != NULL) {
		vQueueDelete(s_gpio_evt_queue);
		s_gpio_evt_queue = NULL;
	}
}
