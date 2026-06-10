#ifndef AUDIO_PCM_PLAYER_H_
#define AUDIO_PCM_PLAYER_H_

#include <stddef.h>
#include <stdint.h>

#include <driver/i2s_std.h>

int audio_pcm_player_play_wav(i2s_chan_handle_t tx_handle, const uint8_t *wav_data, size_t wav_size,
			      uint8_t volume);
int audio_pcm_player_play_silence_ms(i2s_chan_handle_t tx_handle, uint32_t duration_ms);

#endif
