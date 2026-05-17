#ifndef APP_BACKLIGHT_SERVICE_H_
#define APP_BACKLIGHT_SERVICE_H_

#include <stdint.h>

int backlight_service_set_percent(uint8_t percent);
uint8_t backlight_service_get_percent(void);

#endif
