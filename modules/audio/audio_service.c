#include "app_module.h"
#include <app/audio_service.h>
#include <app/hw_config.h>
#include <app/module_common.h>

#include <driver/i2s_std.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <inttypes.h>
#include <string.h>

static const char *TAG = "audio";
static i2s_chan_handle_t s_tx_handle;
static QueueHandle_t s_audio_queue;
static TaskHandle_t s_audio_task;
static uint8_t s_volume = 6;
static int64_t s_audio_blocked_until_us;
static volatile bool s_audio_busy;
static bool s_i2s_enabled;

typedef struct {
	app_audio_event_t event_id;
	const uint8_t *wav_data;
	size_t wav_size;
} app_audio_request_t;

typedef struct {
	const uint8_t *data;
	const uint8_t *end;
} app_audio_clip_t;

extern const uint8_t welcome_wav_start[] asm("_binary_welcome_wav_start");
extern const uint8_t welcome_wav_end[] asm("_binary_welcome_wav_end");
extern const uint8_t connected_wav_start[] asm("_binary_connected_wav_start");
extern const uint8_t connected_wav_end[] asm("_binary_connected_wav_end");
extern const uint8_t env_light_low_wav_start[] asm("_binary_env_light_low_wav_start");
extern const uint8_t env_light_low_wav_end[] asm("_binary_env_light_low_wav_end");
extern const uint8_t env_light_high_wav_start[] asm("_binary_env_light_high_wav_start");
extern const uint8_t env_light_high_wav_end[] asm("_binary_env_light_high_wav_end");
extern const uint8_t env_temp_low_wav_start[] asm("_binary_env_temp_low_wav_start");
extern const uint8_t env_temp_low_wav_end[] asm("_binary_env_temp_low_wav_end");
extern const uint8_t env_temp_high_wav_start[] asm("_binary_env_temp_high_wav_start");
extern const uint8_t env_temp_high_wav_end[] asm("_binary_env_temp_high_wav_end");
extern const uint8_t env_humi_low_wav_start[] asm("_binary_env_humi_low_wav_start");
extern const uint8_t env_humi_low_wav_end[] asm("_binary_env_humi_low_wav_end");
extern const uint8_t env_humi_high_wav_start[] asm("_binary_env_humi_high_wav_start");
extern const uint8_t env_humi_high_wav_end[] asm("_binary_env_humi_high_wav_end");
extern const uint8_t sync_up_wav_start[] asm("_binary_sync_up_wav_start");
extern const uint8_t sync_up_wav_end[] asm("_binary_sync_up_wav_end");
extern const uint8_t rest_reminder_wav_start[] asm("_binary_rest_reminder_wav_start");
extern const uint8_t rest_reminder_wav_end[] asm("_binary_rest_reminder_wav_end");
extern const uint8_t clock_wav_start[] asm("_binary_clock_wav_start");
extern const uint8_t clock_wav_end[] asm("_binary_clock_wav_end");

static const app_audio_clip_t APP_CLIP_WELCOME = {
	.data = welcome_wav_start,
	.end = welcome_wav_end,
};
static const app_audio_clip_t APP_CLIP_CONNECTED = {
	.data = connected_wav_start,
	.end = connected_wav_end,
};
static const app_audio_clip_t APP_CLIP_ENV_LIGHT_LOW = {
	.data = env_light_low_wav_start,
	.end = env_light_low_wav_end,
};
static const app_audio_clip_t APP_CLIP_ENV_LIGHT_HIGH = {
	.data = env_light_high_wav_start,
	.end = env_light_high_wav_end,
};
static const app_audio_clip_t APP_CLIP_ENV_TEMP_LOW = {
	.data = env_temp_low_wav_start,
	.end = env_temp_low_wav_end,
};
static const app_audio_clip_t APP_CLIP_ENV_TEMP_HIGH = {
	.data = env_temp_high_wav_start,
	.end = env_temp_high_wav_end,
};
static const app_audio_clip_t APP_CLIP_ENV_HUMI_LOW = {
	.data = env_humi_low_wav_start,
	.end = env_humi_low_wav_end,
};
static const app_audio_clip_t APP_CLIP_ENV_HUMI_HIGH = {
	.data = env_humi_high_wav_start,
	.end = env_humi_high_wav_end,
};
static const app_audio_clip_t APP_CLIP_TODO_SYNC_UP = {
	.data = sync_up_wav_start,
	.end = sync_up_wav_end,
};
static const app_audio_clip_t APP_CLIP_REST = {
	.data = rest_reminder_wav_start,
	.end = rest_reminder_wav_end,
};
static const app_audio_clip_t APP_CLIP_ALARM = {
	.data = clock_wav_start,
	.end = clock_wav_end,
};

static uint32_t audio_read_le32(const uint8_t *data)
{
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool audio_find_wav_payload(const uint8_t *wav_data, size_t wav_size, const uint8_t **payload, size_t *payload_size)
{
	if (wav_data == NULL || payload == NULL || payload_size == NULL || wav_size < 44U) {
		return false;
	}
	if (memcmp(wav_data, "RIFF", 4) != 0 || memcmp(&wav_data[8], "WAVE", 4) != 0) {
		return false;
	}

	size_t offset = 12U;
	while ((offset + 8U) <= wav_size) {
		const uint8_t *chunk = &wav_data[offset];
		const uint32_t chunk_size = audio_read_le32(&chunk[4]);
		if ((offset + 8U + chunk_size) > wav_size) {
			return false;
		}
		if (memcmp(chunk, "data", 4) == 0) {
			*payload = &chunk[8];
			*payload_size = chunk_size;
			return true;
		}
		offset += 8U + chunk_size + (chunk_size & 1U);
	}

	return false;
}

static void audio_apply_volume(int16_t *samples, size_t sample_count)
{
	for (size_t i = 0; i < sample_count; i++) {
		int32_t scaled = ((int32_t)samples[i] * (int32_t)s_volume) / 10;
		if (scaled > INT16_MAX) {
			scaled = INT16_MAX;
		} else if (scaled < INT16_MIN) {
			scaled = INT16_MIN;
		}
		samples[i] = (int16_t)scaled;
	}
}

static int audio_play_wav_pcm(const uint8_t *wav_data, size_t wav_size)
{
	const uint8_t *payload = NULL;
	size_t payload_size = 0;
	if (!audio_find_wav_payload(wav_data, wav_size, &payload, &payload_size)) {
		ESP_LOGE(TAG, "invalid wav payload");
		return -1;
	}

	const int16_t *samples = (const int16_t *)payload;
	size_t sample_count = payload_size / sizeof(int16_t);
	size_t bytes_written = 0;
	int16_t chunk[256];

	while (sample_count > 0U) {
		const size_t chunk_samples = sample_count > 256U ? 256U : sample_count;
		memcpy(chunk, samples, chunk_samples * sizeof(int16_t));
		audio_apply_volume(chunk, chunk_samples);
		esp_err_t err = i2s_channel_write(s_tx_handle, chunk, chunk_samples * sizeof(int16_t), &bytes_written, 1000);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "i2s_channel_write failed: %s", esp_err_to_name(err));
			return (int)err;
		}
		samples += chunk_samples;
		sample_count -= chunk_samples;
	}

	return 0;
}

static int audio_play_silence_ms(uint32_t duration_ms)
{
	int16_t silence[256] = { 0 };
	size_t bytes_written = 0;
	size_t sample_count = ((size_t)duration_ms * 16000U) / 1000U;

	while (sample_count > 0U) {
		const size_t chunk_samples = sample_count > 256U ? 256U : sample_count;
		esp_err_t err = i2s_channel_write(s_tx_handle, silence, chunk_samples * sizeof(int16_t),
						   &bytes_written, 1000);
		if (err != ESP_OK) {
			ESP_LOGW(TAG, "silence write failed: %s", esp_err_to_name(err));
			return (int)err;
		}
		sample_count -= chunk_samples;
	}

	return 0;
}

static int audio_enable_output(void)
{
	if (s_i2s_enabled) {
		return 0;
	}

	esp_err_t err = i2s_channel_enable(s_tx_handle);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(err));
		return (int)err;
	}
	s_i2s_enabled = true;
	return 0;
}

static void audio_disable_output(void)
{
	if (!s_i2s_enabled) {
		return;
	}

	(void)audio_play_silence_ms(40);
	esp_err_t err = i2s_channel_disable(s_tx_handle);
	if (err != ESP_OK) {
		ESP_LOGW(TAG, "i2s_channel_disable failed: %s", esp_err_to_name(err));
		return;
	}
	s_i2s_enabled = false;
}

static const app_audio_clip_t *audio_get_clip(app_audio_event_t event_id)
{
	switch (event_id) {
	case APP_AUDIO_EVENT_WELCOME:
		return &APP_CLIP_WELCOME;
	case APP_AUDIO_EVENT_WIFI_CONNECTED:
		return &APP_CLIP_CONNECTED;
	case APP_AUDIO_EVENT_ENV_LIGHT_LOW:
		return &APP_CLIP_ENV_LIGHT_LOW;
	case APP_AUDIO_EVENT_ENV_LIGHT_HIGH:
		return &APP_CLIP_ENV_LIGHT_HIGH;
	case APP_AUDIO_EVENT_ENV_TEMP_LOW:
		return &APP_CLIP_ENV_TEMP_LOW;
	case APP_AUDIO_EVENT_ENV_TEMP_HIGH:
		return &APP_CLIP_ENV_TEMP_HIGH;
	case APP_AUDIO_EVENT_ENV_HUMI_LOW:
		return &APP_CLIP_ENV_HUMI_LOW;
	case APP_AUDIO_EVENT_ENV_HUMI_HIGH:
		return &APP_CLIP_ENV_HUMI_HIGH;
	case APP_AUDIO_EVENT_TODO_SYNC_UP:
		return &APP_CLIP_TODO_SYNC_UP;
	case APP_AUDIO_EVENT_REST_REMINDER:
		return &APP_CLIP_REST;
	case APP_AUDIO_EVENT_ALARM:
	case APP_AUDIO_EVENT_HOUR_CHIME:
		return &APP_CLIP_ALARM;
	case APP_AUDIO_EVENT_CONFIRM:
	case APP_AUDIO_EVENT_TEST:
	default:
		return NULL;
	}
}

static void audio_task(void *arg)
{
	(void)arg;

	for (;;) {
		app_audio_request_t request = { 0 };
		if (xQueueReceive(s_audio_queue, &request, portMAX_DELAY) != pdTRUE) {
			continue;
		}

		s_audio_busy = true;
		ESP_LOGI(TAG, "play event=%d size=%u", (int)request.event_id, (unsigned)request.wav_size);
		if (audio_enable_output() == 0) {
			(void)audio_play_wav_pcm(request.wav_data, request.wav_size);
			audio_disable_output();
		}
		s_audio_busy = false;
		s_audio_blocked_until_us = esp_timer_get_time() + 3000000LL;
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
	BaseType_t ok = xTaskCreate(audio_task, "audio_task", 4096, NULL, 8, &s_audio_task);
	if (ok != pdPASS) {
		ESP_LOGE(TAG, "failed to create audio task");
		return -1;
	}

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
		audio_disable_output();
		(void)i2s_del_channel(s_tx_handle);
		s_tx_handle = NULL;
	}

	return 0;
}

static int audio_service_queue_event(app_audio_event_t event_id, const uint8_t *wav_data, size_t wav_size)
{
	if (s_audio_queue == NULL || wav_data == NULL || wav_size == 0U) {
		return -1;
	}
	if (esp_timer_get_time() < s_audio_blocked_until_us) {
		return -1;
	}

	const app_audio_request_t request = {
		.event_id = event_id,
		.wav_data = wav_data,
		.wav_size = wav_size,
	};

	if (xQueueSend(s_audio_queue, &request, 0) != pdTRUE) {
		ESP_LOGW(TAG, "audio queue full");
		return -1;
	}

	return 0;
}

int audio_service_play_test_tone(uint32_t frequency_hz, uint32_t duration_ms)
{
	(void)frequency_hz;
	(void)duration_ms;
	return -1;
}

int audio_service_play_event(app_audio_event_t event_id)
{
	const app_audio_clip_t *clip = audio_get_clip(event_id);
	if (clip == NULL) {
		return -1;
	}

	return audio_service_queue_event(event_id, clip->data, (size_t)(clip->end - clip->data));
}

bool audio_service_is_busy(void)
{
	return s_audio_busy;
}

int audio_service_set_volume(uint8_t volume)
{
	if (volume > 10U) {
		volume = 10U;
	}

	s_volume = volume;
	return 0;
}

uint8_t audio_service_get_volume(void)
{
	return s_volume;
}
