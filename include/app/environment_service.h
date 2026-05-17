#ifndef APP_ENVIRONMENT_SERVICE_H_
#define APP_ENVIRONMENT_SERVICE_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	bool bh1750_valid;
	bool dht11_valid;
	float lux;
	float temperature_c;
	float humidity_percent;
	int64_t updated_at_us;
} app_environment_snapshot_t;

bool environment_service_get_snapshot(app_environment_snapshot_t *out_snapshot);

#endif
