#include "app_module.h"
#include <app/audio_service.h>
#include <app/hw_config.h>
#include <app/module_common.h>

#include <driver/i2s_std.h>
#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <inttypes.h>
#include <string.h>

static const char *TAG = "audio";
static i2s_chan_handle_t s_tx_handle;
static QueueHandle_t s_audio_queue;
static TaskHandle_t s_audio_task;

typedef struct {
	uint32_t frequency_hz;
	uint32_t duration_ms;
} app_audio_request_t;

static void audio_fill_square_wave(int16_t *buffer, size_t sample_count,
	uint32_t sample_rate_hz, uint32_t frequency_hz)
{
	if (frequency_hz == 0U) {
		memset(buffer, 0, sample_count * sizeof(int16_t));
		return;
	}

	const uint32_t period_samples = sample_rate_hz / frequency_hz;
	const uint32_t high_samples = period_samples > 1U ? (period_samples / 2U) : 1U;

	for (size_t i = 0; i < sample_count; i++) {
		const uint32_t phase = period_samples > 0U ? (uint32_t)(i % period_samples) : 0U;
		buffer[i] = (phase < high_samples) ? 12000 : -12000;
	}
}

static int audio_play_square_wave(uint32_t frequency_hz, uint32_t duration_ms)
{
	static const uint32_t sample_rate_hz = 16000;
	int16_t buffer[512];
	size_t bytes_written = 0;
	uint32_t remaining_ms = duration_ms;

	while (remaining_ms > 0U) {
		const uint32_t chunk_ms = remaining_ms > 32U ? 32U : remaining_ms;
		const size_t sample_count = (sample_rate_hz * chunk_ms) / 1000U;
		const size_t byte_count = sample_count * sizeof(int16_t);

		audio_fill_square_wave(buffer, sample_count, sample_rate_hz, frequency_hz);

		esp_err_t err = i2s_channel_write(s_tx_handle, buffer, byte_count, &bytes_written, 100);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "i2s_channel_write failed: %s", esp_err_to_name(err));
			return (int)err;
		}

		remaining_ms -= chunk_ms;
	}

	return 0;
}

static void audio_task(void *arg)
{
	(void)arg;

	for (;;) {
		app_audio_request_t request = { 0 };
		if (xQueueReceive(s_audio_queue, &request, portMAX_DELAY) != pdTRUE) {
			continue;
		}

		ESP_LOGI(TAG, "play tone freq=%" PRIu32 "Hz dur=%" PRIu32 "ms",
			request.frequency_hz, request.duration_ms);
		(void)audio_play_square_wave(request.frequency_hz, request.duration_ms);
	}
}

int audio_service_init(void)
{
	const i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
	const i2s_std_config_t std_cfg = {
		.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
		.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
		.gpio_cfg = {
			.mclk = I2S_GPIO_UNUSED,
			.bclk = APP_PIN_I2S_BCLK,
			.ws = APP_PIN_I2S_WS,
			.dout = APP_PIN_I2S_DOUT,
			.din = I2S_GPIO_UNUSED,
			.invert_flags = {
				.mclk_inv = false,
				.bclk_inv = false,
				.ws_inv = false,
			},
		},
	};

	esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx_handle, NULL);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	err = i2s_channel_init_std_mode(s_tx_handle, &std_cfg);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "i2s_channel_init_std_mode failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	s_audio_queue = xQueueCreate(8, sizeof(app_audio_request_t));
	if (s_audio_queue == NULL) {
		ESP_LOGE(TAG, "failed to create audio queue");
		return -1;
	}

	ESP_LOGI(TAG, "init bclk=%d ws=%d dout=%d", APP_PIN_I2S_BCLK, APP_PIN_I2S_WS, APP_PIN_I2S_DOUT);
	return 0;
}

int audio_service_start(void)
{
	esp_err_t err = i2s_channel_enable(s_tx_handle);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	BaseType_t ok = xTaskCreate(audio_task, "audio_task", 4096, NULL, 8, &s_audio_task);
	if (ok != pdPASS) {
		ESP_LOGE(TAG, "failed to create audio task");
		return -1;
	}

	(void)audio_service_play_test_tone(880, 160);
	return 0;
}

int audio_service_stop(void)
{
	if (s_audio_task != NULL) {
		vTaskDelete(s_audio_task);
		s_audio_task = NULL;
	}

	if (s_audio_queue != NULL) {
		vQueueDelete(s_audio_queue);
		s_audio_queue = NULL;
	}

	if (s_tx_handle != NULL) {
		(void)i2s_channel_disable(s_tx_handle);
		(void)i2s_del_channel(s_tx_handle);
		s_tx_handle = NULL;
	}

	return 0;
}

int audio_service_play_test_tone(uint32_t frequency_hz, uint32_t duration_ms)
{
	if (s_audio_queue == NULL) {
		return -1;
	}

	const app_audio_request_t request = {
		.frequency_hz = frequency_hz,
		.duration_ms = duration_ms,
	};

	if (xQueueSend(s_audio_queue, &request, 0) != pdTRUE) {
		ESP_LOGW(TAG, "audio queue full");
		return -1;
	}

	return 0;
}
