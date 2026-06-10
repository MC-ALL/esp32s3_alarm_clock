#include <sync_transport.h>

#include <app_config.h>
#include <net_service.h>

#include <stdio.h>
#include <time.h>

uint32_t sync_backoff_s(uint8_t failures, uint32_t base_s, uint32_t max_s)
{
	if (failures == 0U) {
		return 0;
	}
	uint32_t value = base_s;
	for (uint8_t i = 1; i < failures && value < max_s; i++) {
		value *= 2U;
	}
	return value > max_s ? max_s : value;
}

bool sync_transport_net_ready(void)
{
	app_net_status_t status = { 0 };
	return net_service_get_status(&status) && status.wifi_connected && status.ip_ready;
}

void sync_transport_format_now_iso(char *out, size_t out_size)
{
	if (out == NULL || out_size == 0U) {
		return;
	}

	time_t now = 0;
	struct tm timeinfo = { 0 };
	time(&now);
	localtime_r(&now, &timeinfo);
	(void)strftime(out, out_size, "%Y-%m-%dT%H:%M:%S%z", &timeinfo);
}

int sync_transport_http_request(const char *method, const char *path, const char *request_body,
				char *response_body, size_t response_body_size, int *out_status)
{
	char url[192];
	(void)snprintf(url, sizeof(url), "http://%s:%d%s", APP_TODO_WEB_HOST, APP_TODO_WEB_PORT, path);

	app_net_http_response_t response = {
		.body = response_body,
		.body_cap = response_body_size,
	};
	if (response_body != NULL && response_body_size > 0U) {
		response_body[0] = '\0';
	}

	int ret = net_service_http_request(method, url, request_body, &response);
	if (out_status != NULL) {
		*out_status = response.status_code;
	}
	return ret;
}
