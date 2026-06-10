#ifndef ENVIRONMENT_DHT11_H_
#define ENVIRONMENT_DHT11_H_

typedef struct {
	const char *name;
	int (*init)(void);
	int (*read)(float *temperature_c, float *humidity_percent);
	void (*deinit)(void);
} app_temp_humidity_provider_t;

const app_temp_humidity_provider_t *environment_dht11_provider(void);

#endif
