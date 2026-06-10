#ifndef APP_SYNC_TRANSPORT_H_
#define APP_SYNC_TRANSPORT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

uint32_t sync_backoff_s(uint8_t failures, uint32_t base_s, uint32_t max_s);
bool sync_transport_net_ready(void);
void sync_transport_format_now_iso(char *out, size_t out_size);
int sync_transport_http_request(const char *method, const char *path, const char *request_body,
				char *response_body, size_t response_body_size, int *out_status);

#endif
