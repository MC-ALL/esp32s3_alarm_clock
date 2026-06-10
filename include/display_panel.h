#ifndef DISPLAY_PANEL_H_
#define DISPLAY_PANEL_H_

#include <stdint.h>

#include <esp_lcd_panel_ops.h>

int display_panel_init(void);
void display_panel_deinit(void);
esp_lcd_panel_handle_t display_panel_handle(void);
int display_panel_fill_color(uint16_t rgb565);

#endif
