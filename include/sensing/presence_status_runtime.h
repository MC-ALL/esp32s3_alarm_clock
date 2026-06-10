#ifndef PRESENCE_STATUS_RUNTIME_H_
#define PRESENCE_STATUS_RUNTIME_H_

#include <stdbool.h>
#include <stdint.h>

#include <sensing/presence_ld2410.h>
#include <sensing/presence_service.h>

#include <freertos/FreeRTOS.h>

typedef struct {
	app_presence_status_t status;
	bool present_hint;
	portMUX_TYPE lock;
} presence_status_runtime_t;

void presence_status_runtime_init(presence_status_runtime_t *runtime, bool out_pin_present);
void presence_status_runtime_apply_frame(presence_status_runtime_t *runtime,
					 const presence_ld2410_frame_t *frame,
					 int64_t now_us);
void presence_status_runtime_refresh(presence_status_runtime_t *runtime,
				     bool out_pin_present,
				     uint32_t rx_bytes,
				     int64_t now_us);
bool presence_status_runtime_get(presence_status_runtime_t *runtime, app_presence_status_t *out_status);
bool presence_status_runtime_present_hint(presence_status_runtime_t *runtime);

#endif
