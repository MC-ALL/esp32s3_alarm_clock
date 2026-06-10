#include <audio/audio_output.h>

#include <audio/audio_pcm_player.h>
#include <core/hw_config.h>

#include <driver/i2s_std.h>
#include <esp_err.h>
#include <esp_log.h>
#include <stdbool.h>

static const char *TAG = "audio_output";

static i2s_chan_handle_t s_tx_handle;
static bool s_i2s_enabled;

static int audio_output_enable(void)
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

static void audio_output_disable(void)
{
	if (!s_i2s_enabled) {
		return;
	}

	(void)audio_pcm_player_play_silence_ms(s_tx_handle, 40);
	esp_err_t err = i2s_channel_disable(s_tx_handle);
	if (err != ESP_OK) {
		ESP_LOGW(TAG, "i2s_channel_disable failed: %s", esp_err_to_name(err));
		return;
	}
	s_i2s_enabled = false;
}

int audio_output_init(void)
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
		(void)i2s_del_channel(s_tx_handle);
		s_tx_handle = NULL;
		return (int)err;
	}

	ESP_LOGI(TAG, "init bclk=%d ws=%d dout=%d", APP_PIN_I2S_BCLK, APP_PIN_I2S_WS, APP_PIN_I2S_DOUT);
	return 0;
}

void audio_output_deinit(void)
{
	if (s_tx_handle == NULL) {
		return;
	}

	audio_output_disable();
	(void)i2s_del_channel(s_tx_handle);
	s_tx_handle = NULL;
}

int audio_output_play_wav(const uint8_t *wav_data, size_t wav_size, uint8_t volume)
{
	if (s_tx_handle == NULL) {
		return -1;
	}

	int ret = audio_output_enable();
	if (ret == 0) {
		ret = audio_pcm_player_play_wav(s_tx_handle, wav_data, wav_size, volume);
		audio_output_disable();
	}
	return ret;
}
