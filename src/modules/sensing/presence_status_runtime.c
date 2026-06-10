#include <presence_status_runtime.h>

#include <freertos/FreeRTOS.h>

#define PRESENCE_FRAME_RECENT_US 1500000LL

void presence_status_runtime_init(presence_status_runtime_t *runtime, bool out_pin_present)
{
	if (runtime == NULL) {
		return;
	}
	*runtime = (presence_status_runtime_t){
		.status = {
			.detected = out_pin_present,
			.out_pin_present = out_pin_present,
			.using_out_fallback = true,
		},
		.present_hint = out_pin_present,
		.lock = portMUX_INITIALIZER_UNLOCKED,
	};
}

void presence_status_runtime_apply_frame(presence_status_runtime_t *runtime,
					 const presence_ld2410_frame_t *frame,
					 int64_t now_us)
{
	if (runtime == NULL || frame == NULL) {
		return;
	}

	taskENTER_CRITICAL(&runtime->lock);
	runtime->status.frame_valid = true;
	runtime->status.target_state = frame->target_state;
	runtime->status.moving_distance_cm = frame->moving_distance_cm;
	runtime->status.moving_energy = frame->moving_energy;
	runtime->status.stationary_distance_cm = frame->stationary_distance_cm;
	runtime->status.stationary_energy = frame->stationary_energy;
	runtime->status.detection_distance_cm = frame->detection_distance_cm;
	runtime->status.max_moving_gate = frame->max_moving_gate;
	runtime->status.max_stationary_gate = frame->max_stationary_gate;
	runtime->status.last_frame_us = now_us;
	taskEXIT_CRITICAL(&runtime->lock);
}

void presence_status_runtime_refresh(presence_status_runtime_t *runtime,
				     bool out_pin_present,
				     uint32_t rx_bytes,
				     int64_t now_us)
{
	if (runtime == NULL) {
		return;
	}

	taskENTER_CRITICAL(&runtime->lock);
	runtime->status.out_pin_present = out_pin_present;
	runtime->status.rx_bytes = rx_bytes;
	runtime->status.updated_at_us = now_us;

	const bool frame_recent =
		runtime->status.frame_valid && ((now_us - runtime->status.last_frame_us) < PRESENCE_FRAME_RECENT_US);
	runtime->status.radar_healthy = frame_recent;
	runtime->status.using_out_fallback = !frame_recent;
	runtime->status.detected = frame_recent ? (runtime->status.target_state != 0U) : out_pin_present;
	runtime->present_hint = runtime->status.detected;
	taskEXIT_CRITICAL(&runtime->lock);
}

bool presence_status_runtime_get(presence_status_runtime_t *runtime, app_presence_status_t *out_status)
{
	if (runtime == NULL || out_status == NULL) {
		return false;
	}

	taskENTER_CRITICAL(&runtime->lock);
	*out_status = runtime->status;
	taskEXIT_CRITICAL(&runtime->lock);
	return true;
}

bool presence_status_runtime_present_hint(presence_status_runtime_t *runtime)
{
	if (runtime == NULL) {
		return false;
	}

	bool present = false;
	taskENTER_CRITICAL(&runtime->lock);
	present = runtime->present_hint;
	taskEXIT_CRITICAL(&runtime->lock);
	return present;
}
