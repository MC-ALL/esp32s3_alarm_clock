#ifndef INPUT_GPIO_H_
#define INPUT_GPIO_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INPUT_GPIO_KEY_COUNT 4U

int input_gpio_init(void);
void input_gpio_deinit(void);
bool input_gpio_read_key_level(size_t key_index);
int input_gpio_wait_edge(uint32_t *gpio_num);
int input_gpio_key_index_from_gpio(uint32_t gpio_num);

#endif
