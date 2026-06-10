#ifndef APP_PRESENCE_SERVICE_H_
#define APP_PRESENCE_SERVICE_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	bool detected;
	bool radar_healthy;
	bool using_out_fallback;
	bool out_pin_present;
	bool frame_valid;
	uint8_t target_state;
	uint16_t moving_distance_cm;
	uint8_t moving_energy;
	uint16_t stationary_distance_cm;
	uint8_t stationary_energy;
	uint16_t detection_distance_cm;
	uint8_t max_moving_gate;
	uint8_t max_stationary_gate;
	uint32_t rx_bytes;
	int64_t updated_at_us;
	int64_t last_frame_us;
} app_presence_status_t;

bool presence_service_is_present_hint(void);
uint32_t presence_service_get_rx_bytes(void);
bool presence_service_get_status(app_presence_status_t *out_status);

#endif
