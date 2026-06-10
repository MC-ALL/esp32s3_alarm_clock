#include <sensing/environment_dht11.h>

#include <core/hw_config.h>

#include <dht.h>
#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
	bool valid;
	float temperature_c;
	float humidity_percent;
	int64_t last_attempt_us;
	int64_t last_success_us;
} dht11_provider_cache_t;

static dht11_provider_cache_t s_dht11_cache;

static int dht11_provider_init(void)
{
	s_dht11_cache = (dht11_provider_cache_t){ 0 };
	gpio_set_direction(APP_PIN_DHT11_DATA, GPIO_MODE_INPUT);
	gpio_pullup_en(APP_PIN_DHT11_DATA);
	return 0;
}

static int dht11_provider_read(float *temperature_c, float *humidity_percent)
{
	static const int64_t DHT11_MIN_SAMPLE_US = 2000000;
	static const int64_t DHT11_STALE_US = 10000000;
	const int64_t now_us = esp_timer_get_time();

	if (temperature_c == NULL || humidity_percent == NULL) {
		return -1;
	}

	if (s_dht11_cache.valid && (now_us - s_dht11_cache.last_attempt_us) < DHT11_MIN_SAMPLE_US) {
		*temperature_c = s_dht11_cache.temperature_c;
		*humidity_percent = s_dht11_cache.humidity_percent;
		return 1;
	}

	s_dht11_cache.last_attempt_us = now_us;
	const esp_err_t err =
	    dht_read_float_data(DHT_TYPE_DHT11, (gpio_num_t)APP_PIN_DHT11_DATA, humidity_percent, temperature_c);
	if (err == ESP_OK) {
		s_dht11_cache.valid = true;
		s_dht11_cache.temperature_c = *temperature_c;
		s_dht11_cache.humidity_percent = *humidity_percent;
		s_dht11_cache.last_success_us = now_us;
		return 0;
	}

	if (s_dht11_cache.valid && (now_us - s_dht11_cache.last_success_us) < DHT11_STALE_US) {
		*temperature_c = s_dht11_cache.temperature_c;
		*humidity_percent = s_dht11_cache.humidity_percent;
		return 1;
	}

	return -(int)err;
}

static void dht11_provider_deinit(void)
{
	gpio_set_direction(APP_PIN_DHT11_DATA, GPIO_MODE_INPUT);
	gpio_pullup_en(APP_PIN_DHT11_DATA);
	s_dht11_cache = (dht11_provider_cache_t){ 0 };
}

static const app_temp_humidity_provider_t DHT11_PROVIDER = {
	.name = "dht11",
	.init = dht11_provider_init,
	.read = dht11_provider_read,
	.deinit = dht11_provider_deinit,
};

const app_temp_humidity_provider_t *environment_dht11_provider(void)
{
	return &DHT11_PROVIDER;
}
