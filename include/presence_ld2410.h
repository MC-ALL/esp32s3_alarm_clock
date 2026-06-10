#ifndef PRESENCE_LD2410_H_
#define PRESENCE_LD2410_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
	uint8_t target_state;
	uint16_t moving_distance_cm;
	uint8_t moving_energy;
	uint16_t stationary_distance_cm;
	uint8_t stationary_energy;
	uint16_t detection_distance_cm;
	uint8_t max_moving_gate;
	uint8_t max_stationary_gate;
} presence_ld2410_frame_t;

typedef struct {
	uint8_t frame_buf[256];
	size_t frame_len;
} presence_ld2410_parser_t;

typedef void (*presence_ld2410_frame_cb_t)(const presence_ld2410_frame_t *frame, void *ctx);

void presence_ld2410_parser_reset(presence_ld2410_parser_t *parser);
bool presence_ld2410_parser_feed(presence_ld2410_parser_t *parser, const uint8_t *data, size_t len,
				 presence_ld2410_frame_cb_t on_frame, void *ctx);

#endif
