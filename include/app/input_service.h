#ifndef APP_INPUT_SERVICE_H_
#define APP_INPUT_SERVICE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
	size_t key_index;
	bool pressed;
	int64_t timestamp_us;
} app_key_event_t;

bool input_service_get_key_level(size_t key_index);

#endif
