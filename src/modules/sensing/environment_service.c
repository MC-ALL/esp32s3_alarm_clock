#include "app_module.h"
#include <app_bus.h>
#include <environment_dht11.h>
#include <environment_service.h>
#include <hw_config.h>
#include <module_common.h>
#include <settings_model.h>

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
static volatile uint32_t s_sample_interval_s = 2;

static int environment_set_sample_interval_s(uint32_t seconds)
{
	if (seconds < 2U) {
		seconds = 2U;
	} else if (seconds > 30U) {
		seconds = 30U;
	}

	if (s_sample_interval_s == seconds) {
		return 0;
	}

	s_sample_interval_s = seconds;
	ESP_LOGI(TAG, "sample interval changed sample_s=%" PRIu32, s_sample_interval_s);
	return 0;
}

static void environment_bus_handler(const app_bus_event_t *event, void *ctx)
{
	(void)ctx;
	if (event == NULL ||
	    (event->type != APP_BUS_EVENT_SETTINGS_CHANGED && event->type != APP_BUS_EVENT_WEB_CONFIG_UPDATED)) {
		return;
	}
	(void)environment_set_sample_interval_s(event->data.settings.settings.env_sample_s);
}

static const app_temp_humidity_provider_t *s_temp_humidity_provider;

static void environment_log_snapshot(const app_environment_snapshot_t *snapshot)
{
	if (snapshot == NULL) {
		return;
	}

	if (snapshot->bh1750_valid && snapshot->dht11_valid) {
		ESP_LOGI(TAG, "sample lux=%.2f temp=%.1f humi=%.1f valid=1/1 ts_us=%" PRIi64, snapshot->lux,
			 snapshot->temperature_c, snapshot->humidity_percent, snapshot->updated_at_us);
		return;
	}

	if (snapshot->bh1750_valid) {
		ESP_LOGI(TAG, "sample lux=%.2f temp=-- humi=-- valid=1/0 ts_us=%" PRIi64, snapshot->lux,
			 snapshot->updated_at_us);
		return;
	}

	if (snapshot->dht11_valid) {
		ESP_LOGI(TAG, "sample lux=-- temp=%.1f humi=%.1f valid=0/1 ts_us=%" PRIi64, snapshot->temperature_c,
			 snapshot->humidity_percent, snapshot->updated_at_us);
		return;
	}

	ESP_LOGI(TAG, "sample lux=-- temp=-- humi=-- valid=0/0 ts_us=%" PRIi64, snapshot->updated_at_us);
}

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

	ESP_LOGI(TAG, "environment task started sample_s=%" PRIu32, s_sample_interval_s);

	for (;;) {
		float lux = 0.0f;
		float temperature_c = 0.0f;
		float humidity_percent = 0.0f;
		const int bh1750_ret = bh1750_measure_lux(&lux);
		const int temp_humi_ret = s_temp_humidity_provider->read(&temperature_c, &humidity_percent);

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

		if (temp_humi_ret >= 0) {
			if (temp_humi_ret == 0 && s_dht11_fail_streak > 0U) {
				ESP_LOGI(TAG, "dht11 recovered after %" PRIu32 " failures", s_dht11_fail_streak);
			}
			s_snapshot.dht11_valid = true;
			s_snapshot.temperature_c = temperature_c;
			s_snapshot.humidity_percent = humidity_percent;
			if (temp_humi_ret == 0) {
				s_dht11_fail_streak = 0;
			}
		} else {
			s_snapshot.dht11_valid = false;
			s_dht11_fail_streak++;
			if (s_dht11_fail_streak == 1U || (s_dht11_fail_streak % 10U) == 0U) {
				ESP_LOGW(TAG, "dht11 read failed: %d, gpio=%d level=%d streak=%" PRIu32, temp_humi_ret,
					 APP_PIN_DHT11_DATA, gpio_get_level(APP_PIN_DHT11_DATA), s_dht11_fail_streak);
			}
		}

		s_snapshot.updated_at_us = esp_timer_get_time();
		xSemaphoreGive(s_snapshot_mutex);

		environment_log_snapshot(&s_snapshot);
		vTaskDelay(pdMS_TO_TICKS(s_sample_interval_s * 1000U));
	}
}

int environment_service_init(void)
{
	(void)esp_log_level_set("dht", ESP_LOG_WARN);
	s_temp_humidity_provider = environment_dht11_provider();

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
		(void)i2c_del_master_bus(s_i2c_bus);
		s_i2c_bus = NULL;
		return (int)err;
	}

	err = s_temp_humidity_provider->init();
	if (err != 0) {
		ESP_LOGE(TAG, "%s init failed: %d", s_temp_humidity_provider->name, err);
		(void)i2c_master_bus_rm_device(s_bh1750_dev);
		s_bh1750_dev = NULL;
		(void)i2c_del_master_bus(s_i2c_bus);
		s_i2c_bus = NULL;
		return err;
	}

	s_snapshot = (app_environment_snapshot_t){ 0 };
	s_dht11_fail_streak = 0;
	app_settings_t settings = { 0 };
	settings_model_get(&settings);
	(void)environment_set_sample_interval_s(settings.env_sample_s);
	s_snapshot_mutex = xSemaphoreCreateMutex();
	if (s_snapshot_mutex == NULL) {
		ESP_LOGE(TAG, "failed to create snapshot mutex");
		s_temp_humidity_provider->deinit();
		(void)i2c_master_bus_rm_device(s_bh1750_dev);
		s_bh1750_dev = NULL;
		(void)i2c_del_master_bus(s_i2c_bus);
		s_i2c_bus = NULL;
		return -1;
	}
	(void)app_bus_subscribe(APP_BUS_EVENT_SETTINGS_CHANGED, environment_bus_handler, NULL);
	(void)app_bus_subscribe(APP_BUS_EVENT_WEB_CONFIG_UPDATED, environment_bus_handler, NULL);

	ESP_LOGI(TAG,
		 "init bh1750 sda=%d scl=%d addr=0x%02X temp_humi=%s gpio=%d "
		 "dht_driver=dht.h sample_s=%" PRIu32,
		 APP_PIN_BH1750_SDA, APP_PIN_BH1750_SCL, APP_BH1750_I2C_ADDR, s_temp_humidity_provider->name,
		 APP_PIN_DHT11_DATA, s_sample_interval_s);
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

	if (s_temp_humidity_provider != NULL && s_temp_humidity_provider->deinit != NULL) {
		s_temp_humidity_provider->deinit();
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

uint32_t environment_service_get_sample_interval_s(void)
{
	return s_sample_interval_s;
}
