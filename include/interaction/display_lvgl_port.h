#ifndef APP_DISPLAY_LVGL_PORT_H_
#define APP_DISPLAY_LVGL_PORT_H_

#include <stdbool.h>
#include <stdint.h>

#include <esp_lcd_panel_ops.h>
#include <lvgl.h>

int display_lvgl_port_init(esp_lcd_panel_handle_t panel);
int display_lvgl_port_start(void);
void display_lvgl_port_stop(void);
bool display_lvgl_port_lock(uint32_t timeout_ms);
void display_lvgl_port_unlock(void);
lv_obj_t *display_lvgl_port_get_screen(void);

#endif
