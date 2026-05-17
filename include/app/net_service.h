#ifndef APP_NET_SERVICE_H_
#define APP_NET_SERVICE_H_

#include <stdbool.h>

typedef struct {
	bool wifi_started;
	bool wifi_connected;
	bool ip_ready;
	bool time_synced;
} app_net_status_t;

bool net_service_get_status(app_net_status_t *out_status);

#endif
