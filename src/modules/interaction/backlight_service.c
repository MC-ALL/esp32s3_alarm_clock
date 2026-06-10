#include "app_module.h"
#include <backlight_service.h>
#include <hw_config.h>
#include <module_common.h>

#include <driver/ledc.h>
#include <esp_err.h>
#include <esp_log.h>

static const char *TAG = "backlight";
static bool s_ready;
static uint8_t s_percent;

static uint32_t backlight_percent_to_duty(uint8_t percent)
{
	const uint32_t max_duty = (1U << LEDC_TIMER_10_BIT) - 1U;

	return (max_duty * percent) / 100U;
}

static int backlight_apply_percent(uint8_t percent)
{
	if (!s_ready) {
		return -1;
	}

	if (percent > 100U) {
		percent = 100U;
	}

	esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0,
		backlight_percent_to_duty(percent));
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "ledc_set_duty failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	err = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "ledc_update_duty failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	s_percent = percent;
	return 0;
}

int backlight_service_init(void)
{
	const ledc_timer_config_t timer_cfg = {
		.speed_mode = LEDC_LOW_SPEED_MODE,
		.duty_resolution = LEDC_TIMER_10_BIT,
		.timer_num = LEDC_TIMER_0,
		.freq_hz = 5000,
		.clk_cfg = LEDC_AUTO_CLK,
	};
	const ledc_channel_config_t channel_cfg = {
		.gpio_num = APP_PIN_BACKLIGHT_PWM,
		.speed_mode = LEDC_LOW_SPEED_MODE,
		.channel = LEDC_CHANNEL_0,
		.intr_type = LEDC_INTR_DISABLE,
		.timer_sel = LEDC_TIMER_0,
		.duty = 0,
		.hpoint = 0,
	};

	esp_err_t err = ledc_timer_config(&timer_cfg);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "ledc_timer_config failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	err = ledc_channel_config(&channel_cfg);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "ledc_channel_config failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	s_ready = true;
	s_percent = 0;
	ESP_LOGI(TAG, "init on GPIO%d", APP_PIN_BACKLIGHT_PWM);
	return 0;
}

int backlight_service_start(void)
{
	return backlight_apply_percent(15);
}

int backlight_service_stop(void)
{
	if (!s_ready) {
		return 0;
	}

	(void)backlight_apply_percent(0);
	ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
	s_ready = false;
	return 0;
}

int backlight_service_set_percent(uint8_t percent)
{
	return backlight_apply_percent(percent);
}

uint8_t backlight_service_get_percent(void)
{
	return s_percent;
}
