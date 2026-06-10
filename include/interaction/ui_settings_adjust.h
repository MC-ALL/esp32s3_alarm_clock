#ifndef UI_SETTINGS_ADJUST_H_
#define UI_SETTINGS_ADJUST_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <interaction/ui_settings_controller.h>

bool ui_settings_cycle_u8(uint8_t *value, const uint8_t *values, size_t count, uint8_t fallback);
bool ui_settings_cycle_u16(uint16_t *value, const uint16_t *values, size_t count, uint16_t fallback);
bool ui_settings_cycle_i16(int16_t *value, const int16_t *values, size_t count, int16_t fallback);
void ui_settings_adjust_process_env(const ui_settings_controller_state_t *state);

#endif
