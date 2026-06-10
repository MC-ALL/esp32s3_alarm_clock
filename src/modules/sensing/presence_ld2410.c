#include <presence_ld2410.h>

#include <string.h>

static uint16_t presence_read_le16(const uint8_t *data)
{
	return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static bool presence_parse_payload(const uint8_t *payload, size_t payload_len, presence_ld2410_frame_t *frame)
{
	if (payload == NULL || frame == NULL || payload_len < 13U) {
		return false;
	}
	if ((payload[0] != 0x01U && payload[0] != 0x02U) || payload[1] != 0xAAU) {
		return false;
	}
	if (payload[payload_len - 2U] != 0x55U || payload[payload_len - 1U] != 0x00U) {
		return false;
	}

	*frame = (presence_ld2410_frame_t){
		.target_state = payload[2],
		.moving_distance_cm = presence_read_le16(&payload[3]),
		.moving_energy = payload[5],
		.stationary_distance_cm = presence_read_le16(&payload[6]),
		.stationary_energy = payload[8],
		.detection_distance_cm = presence_read_le16(&payload[9]),
	};

	if (payload[0] == 0x01U && payload_len >= 35U) {
		frame->max_moving_gate = payload[11];
		frame->max_stationary_gate = payload[12];
	}
	return true;
}

void presence_ld2410_parser_reset(presence_ld2410_parser_t *parser)
{
	if (parser != NULL) {
		parser->frame_len = 0;
	}
}

bool presence_ld2410_parser_feed(presence_ld2410_parser_t *parser, const uint8_t *data, size_t len,
				 presence_ld2410_frame_cb_t on_frame, void *ctx)
{
	static const uint8_t FRAME_HEADER[4] = { 0xF4, 0xF3, 0xF2, 0xF1 };
	static const uint8_t FRAME_TAIL[4] = { 0xF8, 0xF7, 0xF6, 0xF5 };

	if (parser == NULL || data == NULL || len == 0U) {
		return false;
	}

	bool overflowed = false;
	if ((parser->frame_len + len) > sizeof(parser->frame_buf)) {
		parser->frame_len = 0;
		overflowed = true;
	}

	if (len > sizeof(parser->frame_buf)) {
		data += (len - sizeof(parser->frame_buf));
		len = sizeof(parser->frame_buf);
	}

	memcpy(&parser->frame_buf[parser->frame_len], data, len);
	parser->frame_len += len;

	for (;;) {
		if (parser->frame_len < 6U) {
			return !overflowed;
		}

		size_t start = 0;
		while ((start + sizeof(FRAME_HEADER)) <= parser->frame_len) {
			if (memcmp(&parser->frame_buf[start], FRAME_HEADER, sizeof(FRAME_HEADER)) == 0) {
				break;
			}
			start++;
		}

		if (start > 0U) {
			if (start >= parser->frame_len) {
				parser->frame_len = 0;
				return !overflowed;
			}
			memmove(parser->frame_buf, &parser->frame_buf[start], parser->frame_len - start);
			parser->frame_len -= start;
			if (parser->frame_len < 6U) {
				return !overflowed;
			}
		}

		const uint16_t payload_len = presence_read_le16(&parser->frame_buf[4]);
		if (payload_len < 13U || payload_len > 64U) {
			memmove(parser->frame_buf, &parser->frame_buf[1], parser->frame_len - 1U);
			parser->frame_len--;
			continue;
		}

		const size_t total_len = 6U + (size_t)payload_len + sizeof(FRAME_TAIL);
		if (parser->frame_len < total_len) {
			return !overflowed;
		}

		if (memcmp(&parser->frame_buf[6U + payload_len], FRAME_TAIL, sizeof(FRAME_TAIL)) != 0) {
			memmove(parser->frame_buf, &parser->frame_buf[1], parser->frame_len - 1U);
			parser->frame_len--;
			continue;
		}

		presence_ld2410_frame_t frame = { 0 };
		if (presence_parse_payload(&parser->frame_buf[6], payload_len, &frame) && on_frame != NULL) {
			on_frame(&frame, ctx);
		}
		if (parser->frame_len > total_len) {
			memmove(parser->frame_buf, &parser->frame_buf[total_len], parser->frame_len - total_len);
		}
		parser->frame_len -= total_len;
	}
}
