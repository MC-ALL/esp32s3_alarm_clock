#ifndef UI_STATUS_PAGES_H_
#define UI_STATUS_PAGES_H_

#include <lvgl.h>
#include <ui_pages.h>

void ui_status_pages_render_env(lv_obj_t *screen, const ui_pages_state_t *state);
void ui_status_pages_render_wifi(lv_obj_t *screen);

#endif
