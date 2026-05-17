#ifndef APP_PRESENCE_SERVICE_H_
#define APP_PRESENCE_SERVICE_H_

#include <stdbool.h>
#include <stdint.h>

bool presence_service_is_present_hint(void);
uint32_t presence_service_get_rx_bytes(void);

#endif
