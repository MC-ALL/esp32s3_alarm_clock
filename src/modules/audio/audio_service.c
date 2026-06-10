#include "app_module.h"
#include <app_bus.h>
#include <audio_assets.h>
#include <audio_output.h>
#include <audio_service.h>
#include <module_common.h>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static const char *TAG = "audio";
static QueueHandle_t s_audio_queue;
static TaskHandle_t s_audio_task;
static uint8_t s_volume = 6;
static int64_t s_audio_blocked_until_us;
static volatile bool s_audio_busy;

typedef struct {
	app_audio_event_t event_id;
	const uint8_t *wav_data;
	size_t wav_size;
} app_audio_request_t;

static void audio_bus_handler(const app_bus_event_t *event, void *ctx);

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
		(void)audio_output_play_wav(request.wav_data, request.wav_size, s_volume);
		s_audio_busy = false;
		s_audio_blocked_until_us = esp_timer_get_time() + 3000000LL;
	}
}

int audio_service_init(void)
{
	int ret = audio_output_init();
	if (ret != 0) {
		return ret;
	}

	s_audio_queue = xQueueCreate(8, sizeof(app_audio_request_t));
	if (s_audio_queue == NULL) {
		ESP_LOGE(TAG, "failed to create audio queue");
		audio_output_deinit();
		return -1;
	}
	(void)app_bus_subscribe(APP_BUS_EVENT_AUDIO_PLAY_REQUEST, audio_bus_handler, NULL);

	ESP_LOGI(TAG, "init");
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

	audio_output_deinit();

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

static int audio_service_play_event(app_audio_event_t event_id)
{
	const app_audio_clip_t *clip = audio_assets_get_clip(event_id);
	if (clip == NULL) {
		return -1;
	}

	return audio_service_queue_event(event_id, clip->data, (size_t)(clip->end - clip->data));
}

static void audio_bus_handler(const app_bus_event_t *event, void *ctx)
{
	(void)ctx;
	if (event == NULL || event->type != APP_BUS_EVENT_AUDIO_PLAY_REQUEST) {
		return;
	}
	int ret = audio_service_play_event(event->data.audio.event_id);
	ESP_LOGI(TAG, "bus play event=%d ret=%d", (int)event->data.audio.event_id, ret);
}
