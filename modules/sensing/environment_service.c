#include "app_module.h"
#include <app/environment_service.h>
#include <app/hw_config.h>
#include <app/module_common.h>

#include <dht.h>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <inttypes.h>

static const char *TAG = "env";
static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_bh1750_dev;
static TaskHandle_t s_env_task;
static app_environment_snapshot_t s_snapshot;
static SemaphoreHandle_t s_snapshot_mutex;
static uint32_t s_dht11_fail_streak;

static int bh1750_measure_lux(float *lux_out)
{
	static const uint8_t bh1750_cmd = 0x10;
	uint8_t raw[2] = { 0 };

	esp_err_t err = i2c_master_transmit(s_bh1750_dev, &bh1750_cmd, sizeof(bh1750_cmd), 100);
	if (err != ESP_OK) {
		return (int)err;
	}

	vTaskDelay(pdMS_TO_TICKS(180));

	err = i2c_master_receive(s_bh1750_dev, raw, sizeof(raw), 100);
	if (err != ESP_OK) {
		return (int)err;
	}

	const uint16_t level = ((uint16_t)raw[0] << 8) | raw[1];
	*lux_out = (float)level / 1.2f;
	return 0;
}

static void environment_task(void *arg)
{
	(void)arg;

	for (;;) {
		float lux = 0.0f;
		float temperature_c = 0.0f;
		float humidity_percent = 0.0f;
		const int bh1750_ret = bh1750_measure_lux(&lux);
		const esp_err_t dht11_ret = dht_read_float_data(
			DHT_TYPE_DHT11,
			(gpio_num_t)APP_PIN_DHT11_DATA,
			&humidity_percent,
			&temperature_c);

		if (xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
			vTaskDelay(pdMS_TO_TICKS(1000));
			continue;
		}

		if (bh1750_ret == 0) {
			s_snapshot.bh1750_valid = true;
			s_snapshot.lux = lux;
		} else {
			s_snapshot.bh1750_valid = false;
			ESP_LOGW(TAG, "bh1750 read failed: %d", bh1750_ret);
		}

		if (dht11_ret == ESP_OK) {
			if (s_dht11_fail_streak > 0U) {
				ESP_LOGI(TAG, "dht11 recovered after %" PRIu32 " failures", s_dht11_fail_streak);
			}
			s_snapshot.dht11_valid = true;
			s_snapshot.temperature_c = temperature_c;
			s_snapshot.humidity_percent = humidity_percent;
			s_dht11_fail_streak = 0;
		} else {
			s_snapshot.dht11_valid = false;
			s_dht11_fail_streak++;
			if (s_dht11_fail_streak == 1U || (s_dht11_fail_streak % 10U) == 0U) {
				ESP_LOGW(TAG,
					"dht11 read failed: %s (%d), gpio=%d level=%d streak=%" PRIu32,
					esp_err_to_name(dht11_ret), (int)dht11_ret, APP_PIN_DHT11_DATA,
					gpio_get_level(APP_PIN_DHT11_DATA), s_dht11_fail_streak);
			}
		}

		s_snapshot.updated_at_us = esp_timer_get_time();
		xSemaphoreGive(s_snapshot_mutex);

		ESP_LOGI(TAG, "env lux=%.2f temp=%.1f humi=%.1f",
			s_snapshot.lux, s_snapshot.temperature_c, s_snapshot.humidity_percent);
		vTaskDelay(pdMS_TO_TICKS(2000));
	}
}

int environment_service_init(void)
{
	const i2c_master_bus_config_t bus_config = {
		.i2c_port = APP_BH1750_I2C_PORT,
		.sda_io_num = APP_PIN_BH1750_SDA,
		.scl_io_num = APP_PIN_BH1750_SCL,
		.clk_source = I2C_CLK_SRC_DEFAULT,
		.glitch_ignore_cnt = 7,
		.flags.enable_internal_pullup = true,
	};
	const i2c_device_config_t dev_config = {
		.dev_addr_length = I2C_ADDR_BIT_LEN_7,
		.device_address = APP_BH1750_I2C_ADDR,
		.scl_speed_hz = APP_BH1750_I2C_HZ,
	};

	esp_err_t err = i2c_new_master_bus(&bus_config, &s_i2c_bus);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	err = i2c_master_bus_add_device(s_i2c_bus, &dev_config, &s_bh1750_dev);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	s_snapshot = (app_environment_snapshot_t){ 0 };
	s_dht11_fail_streak = 0;
	s_snapshot_mutex = xSemaphoreCreateMutex();
	if (s_snapshot_mutex == NULL) {
		ESP_LOGE(TAG, "failed to create snapshot mutex");
		return -1;
	}

	ESP_LOGI(TAG, "init bh1750 sda=%d scl=%d addr=0x%02X dht11_gpio=%d dht_driver=esp-idf-lib/dht type=DHT11 sample_ms=2000",
		APP_PIN_BH1750_SDA, APP_PIN_BH1750_SCL, APP_BH1750_I2C_ADDR, APP_PIN_DHT11_DATA);
	return 0;
}

int environment_service_start(void)
{
	BaseType_t ok = xTaskCreate(environment_task, "env_task", 4096, NULL, 8, &s_env_task);
	if (ok != pdPASS) {
		ESP_LOGE(TAG, "failed to create env task");
		return -1;
	}

	return 0;
}

int environment_service_stop(void)
{
	if (s_env_task != NULL) {
		vTaskDelete(s_env_task);
		s_env_task = NULL;
	}

	if (s_bh1750_dev != NULL) {
		(void)i2c_master_bus_rm_device(s_bh1750_dev);
		s_bh1750_dev = NULL;
	}

	if (s_i2c_bus != NULL) {
		(void)i2c_del_master_bus(s_i2c_bus);
		s_i2c_bus = NULL;
	}

	if (s_snapshot_mutex != NULL) {
		vSemaphoreDelete(s_snapshot_mutex);
		s_snapshot_mutex = NULL;
	}

	return 0;
}

bool environment_service_get_snapshot(app_environment_snapshot_t *out_snapshot)
{
	if (out_snapshot == NULL) {
		return false;
	}

	if (s_snapshot_mutex != NULL) {
		if (xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
			return false;
		}
		*out_snapshot = s_snapshot;
		xSemaphoreGive(s_snapshot_mutex);
	} else {
		*out_snapshot = s_snapshot;
	}

	return s_snapshot.bh1750_valid || s_snapshot.dht11_valid;
}
