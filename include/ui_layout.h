#ifndef APP_UI_LAYOUT_H_
#define APP_UI_LAYOUT_H_

#include <lvgl.h>

lv_color_t ui_color_black(void);
lv_color_t ui_color_white(void);
lv_color_t ui_color_blue(void);
lv_color_t ui_color_orange(void);
lv_color_t ui_color_purple(void);
lv_color_t ui_color_teal(void);
lv_color_t ui_color_green(void);
lv_color_t ui_color_dark(void);
lv_color_t ui_color_gray(void);
lv_color_t ui_color_red(void);
lv_color_t ui_color_yellow(void);
lv_color_t ui_color_humi(void);
lv_color_t ui_color_lux(void);
lv_color_t ui_color_ip(void);
lv_color_t ui_color_sync(void);
lv_color_t ui_color_todo_net(void);
lv_color_t ui_color_panel(void);

void ui_obj_plain(lv_obj_t *obj, lv_color_t bg);
lv_obj_t *ui_box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t bg);
lv_obj_t *ui_scroll_area(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t bg);
lv_obj_t *ui_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color,
		   int32_t x, int32_t y, int32_t w, int32_t h, lv_text_align_t align);
lv_obj_t *ui_label_wrap(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color,
			int32_t x, int32_t y, int32_t w, int32_t h, lv_text_align_t align);

#endif
