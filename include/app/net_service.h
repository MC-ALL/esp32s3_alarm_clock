#ifndef APP_NET_SERVICE_H_
#define APP_NET_SERVICE_H_

#include <stdbool.h>
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
	char ssid[33];
	int8_t rssi;
	uint8_t authmode;
} app_net_scan_record_t;

#define APP_NET_SCAN_MAX_RESULTS 5

typedef struct {
	bool scan_in_progress;
	bool scan_ready;
	uint8_t count;
	app_net_scan_record_t records[APP_NET_SCAN_MAX_RESULTS];
} app_net_scan_snapshot_t;

bool net_service_get_status(app_net_status_t *out_status);
bool net_service_get_scan_snapshot(app_net_scan_snapshot_t *out_snapshot);
int net_service_request_scan(void);

#endif
