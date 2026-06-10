#ifndef ENVIRONMENT_SAMPLE_RUNTIME_H_
#define ENVIRONMENT_SAMPLE_RUNTIME_H_

#include <stdint.h>

#include <environment_service.h>

typedef struct {
	int bh1750_ret;
	float lux;
	int temp_humi_ret;
	float temperature_c;
	float humidity_percent;
} environment_sample_result_t;

typedef struct {
	uint32_t dht11_fail_streak;
} environment_sample_runtime_t;

void environment_sample_runtime_init(environment_sample_runtime_t *runtime);
void environment_sample_runtime_apply(environment_sample_runtime_t *runtime,
				      app_environment_snapshot_t *snapshot,
				      const environment_sample_result_t *sample);
void environment_sample_runtime_log_snapshot(const app_environment_snapshot_t *snapshot);

#endif
