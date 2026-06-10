#ifndef NET_WIFI_PLATFORM_H_
#define NET_WIFI_PLATFORM_H_

#include <stdbool.h>

#include <esp_event.h>

int net_wifi_platform_init(esp_event_handler_t event_handler, void *handler_arg);
int net_wifi_platform_configure_sta(const char *ssid, const char *password);
void net_wifi_platform_deinit(void);
bool net_wifi_platform_is_initialized(void);

#endif
