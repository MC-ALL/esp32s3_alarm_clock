#ifndef AUDIO_OUTPUT_H_
#define AUDIO_OUTPUT_H_

#include <stddef.h>
#include <stdint.h>

int audio_output_init(void);
void audio_output_deinit(void);
int audio_output_play_wav(const uint8_t *wav_data, size_t wav_size, uint8_t volume);

#endif
