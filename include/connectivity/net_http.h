#ifndef NET_HTTP_H_
#define NET_HTTP_H_

#include <connectivity/net_service.h>

int net_http_request(const char *method, const char *url, const char *body, app_net_http_response_t *response);

#endif
