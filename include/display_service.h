#ifndef APP_DISPLAY_SERVICE_H_
#define APP_DISPLAY_SERVICE_H_

#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>

bool display_service_is_ready(void);
bool display_service_lock(uint32_t timeout_ms);
void display_service_unlock(void);
lv_obj_t *display_service_get_screen(void);
int display_service_fill_color(uint16_t rgb565);

#endif
