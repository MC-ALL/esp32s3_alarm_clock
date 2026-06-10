#ifndef APP_NET_SERVICE_H_
#define APP_NET_SERVICE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
	bool wifi_started;
	bool wifi_connected;
	bool ip_ready;
	bool time_synced;
	char connected_ssid[33];
	char ip_addr[16];
	int8_t rssi;
} app_net_status_t;

typedef struct {
	char *body;
	size_t body_len;
	size_t body_cap;
	int status_code;
} app_net_http_response_t;

bool net_service_get_status(app_net_status_t *out_status);
int net_service_request_connect_now(void);
int net_service_http_request(const char *method, const char *url, const char *body,
			     app_net_http_response_t *response);

#endif
