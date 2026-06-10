#include <environment_sample_runtime.h>

#include <hw_config.h>

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <inttypes.h>

static const char *TAG = "env";

void environment_sample_runtime_init(environment_sample_runtime_t *runtime)
{
	if (runtime == NULL) {
		return;
	}
	*runtime = (environment_sample_runtime_t){ 0 };
}

void environment_sample_runtime_apply(environment_sample_runtime_t *runtime,
				      app_environment_snapshot_t *snapshot,
				      const environment_sample_result_t *sample)
{
	if (runtime == NULL || snapshot == NULL || sample == NULL) {
		return;
	}

	if (sample->bh1750_ret == 0) {
		snapshot->bh1750_valid = true;
		snapshot->lux = sample->lux;
	} else {
		snapshot->bh1750_valid = false;
		ESP_LOGW(TAG, "bh1750 read failed: %d", sample->bh1750_ret);
	}

	if (sample->temp_humi_ret >= 0) {
		if (sample->temp_humi_ret == 0 && runtime->dht11_fail_streak > 0U) {
			ESP_LOGI(TAG, "dht11 recovered after %" PRIu32 " failures", runtime->dht11_fail_streak);
		}
		snapshot->dht11_valid = true;
		snapshot->temperature_c = sample->temperature_c;
		snapshot->humidity_percent = sample->humidity_percent;
		if (sample->temp_humi_ret == 0) {
			runtime->dht11_fail_streak = 0;
		}
	} else {
		snapshot->dht11_valid = false;
		runtime->dht11_fail_streak++;
		if (runtime->dht11_fail_streak == 1U || (runtime->dht11_fail_streak % 10U) == 0U) {
			ESP_LOGW(TAG, "dht11 read failed: %d, gpio=%d level=%d streak=%" PRIu32,
				 sample->temp_humi_ret,
				 APP_PIN_DHT11_DATA,
				 gpio_get_level(APP_PIN_DHT11_DATA),
				 runtime->dht11_fail_streak);
		}
	}

	snapshot->updated_at_us = esp_timer_get_time();
}

void environment_sample_runtime_log_snapshot(const app_environment_snapshot_t *snapshot)
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
