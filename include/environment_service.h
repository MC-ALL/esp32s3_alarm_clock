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
uint32_t environment_service_get_sample_interval_s(void);
int environment_service_set_sample_interval_s(uint32_t seconds);

#endif
