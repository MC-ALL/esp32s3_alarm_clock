#ifndef APP_DISPLAY_SERVICE_H_
#define APP_DISPLAY_SERVICE_H_

#include <stdbool.h>
#include <stdint.h>

bool display_service_is_ready(void);
int display_service_fill_color(uint16_t rgb565);

#endif
