#ifndef UI_KEY_QUEUE_H_
#define UI_KEY_QUEUE_H_

#include <stdbool.h>
#include <stddef.h>

int ui_key_queue_init(void);
bool ui_key_queue_receive(size_t *key_index);
void ui_key_queue_deinit(void);

#endif
