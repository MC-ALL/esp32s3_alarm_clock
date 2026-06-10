#ifndef AUDIO_ASSETS_H_
#define AUDIO_ASSETS_H_

#include <audio_service.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
	const uint8_t *data;
	const uint8_t *end;
} app_audio_clip_t;

const app_audio_clip_t *audio_assets_get_clip(app_audio_event_t event_id);

#endif
