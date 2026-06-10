#include <audio/audio_pcm_player.h>

#include <esp_err.h>
#include <esp_log.h>
#include <stdbool.h>
#include <string.h>

static const char *TAG = "audio_pcm";

static uint32_t audio_read_le32(const uint8_t *data)
{
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool audio_find_wav_payload(const uint8_t *wav_data, size_t wav_size, const uint8_t **payload,
				   size_t *payload_size)
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

static void audio_apply_volume(int16_t *samples, size_t sample_count, uint8_t volume)
{
	for (size_t i = 0; i < sample_count; i++) {
		int32_t scaled = ((int32_t)samples[i] * (int32_t)volume) / 10;
		if (scaled > INT16_MAX) {
			scaled = INT16_MAX;
		} else if (scaled < INT16_MIN) {
			scaled = INT16_MIN;
		}
		samples[i] = (int16_t)scaled;
	}
}

int audio_pcm_player_play_wav(i2s_chan_handle_t tx_handle, const uint8_t *wav_data, size_t wav_size,
			      uint8_t volume)
{
	if (tx_handle == NULL) {
		return -1;
	}

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
		audio_apply_volume(chunk, chunk_samples, volume);
		esp_err_t err = i2s_channel_write(tx_handle, chunk, chunk_samples * sizeof(int16_t), &bytes_written,
						   1000);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "i2s_channel_write failed: %s", esp_err_to_name(err));
			return (int)err;
		}
		samples += chunk_samples;
		sample_count -= chunk_samples;
	}

	return 0;
}

int audio_pcm_player_play_silence_ms(i2s_chan_handle_t tx_handle, uint32_t duration_ms)
{
	if (tx_handle == NULL) {
		return -1;
	}

	int16_t silence[256] = { 0 };
	size_t bytes_written = 0;
	size_t sample_count = ((size_t)duration_ms * 16000U) / 1000U;

	while (sample_count > 0U) {
		const size_t chunk_samples = sample_count > 256U ? 256U : sample_count;
		esp_err_t err = i2s_channel_write(tx_handle, silence, chunk_samples * sizeof(int16_t),
						   &bytes_written, 1000);
		if (err != ESP_OK) {
			ESP_LOGW(TAG, "silence write failed: %s", esp_err_to_name(err));
			return (int)err;
		}
		sample_count -= chunk_samples;
	}

	return 0;
}
