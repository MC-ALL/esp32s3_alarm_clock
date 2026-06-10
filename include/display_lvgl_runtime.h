#ifndef DISPLAY_LVGL_RUNTIME_H_
#define DISPLAY_LVGL_RUNTIME_H_

#include <stdbool.h>
#include <stdint.h>

typedef bool (*display_lvgl_runtime_lock_fn_t)(uint32_t timeout_ms);
typedef void (*display_lvgl_runtime_unlock_fn_t)(void);

int display_lvgl_runtime_init(display_lvgl_runtime_lock_fn_t lock_fn,
			      display_lvgl_runtime_unlock_fn_t unlock_fn);
int display_lvgl_runtime_start(void);
void display_lvgl_runtime_stop(void);
void display_lvgl_runtime_handle_timer(void);

#endif
