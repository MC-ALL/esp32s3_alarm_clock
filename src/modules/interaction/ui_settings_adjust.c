#include <interaction/ui_settings_adjust.h>

#include <interaction/ui_actions.h>

bool ui_settings_cycle_u8(uint8_t *value, const uint8_t *values, size_t count, uint8_t fallback)
{
	if (value == NULL || values == NULL || count == 0U) {
		return false;
	}

	for (size_t i = 0; i < count; i++) {
		if (*value == values[i]) {
			*value = values[(i + 1U) % count];
			return true;
		}
	}
	*value = fallback;
	return false;
}

bool ui_settings_cycle_u16(uint16_t *value, const uint16_t *values, size_t count, uint16_t fallback)
{
	if (value == NULL || values == NULL || count == 0U) {
		return false;
	}

	for (size_t i = 0; i < count; i++) {
		if (*value == values[i]) {
			*value = values[(i + 1U) % count];
			return true;
		}
	}
	*value = fallback;
	return false;
}

bool ui_settings_cycle_i16(int16_t *value, const int16_t *values, size_t count, int16_t fallback)
{
	if (value == NULL || values == NULL || count == 0U) {
		return false;
	}

	for (size_t i = 0; i < count; i++) {
		if (*value == values[i]) {
			*value = values[(i + 1U) % count];
			return true;
		}
	}
	*value = fallback;
	return false;
}

void ui_settings_adjust_process_env(const ui_settings_controller_state_t *state)
{
	if (state == NULL || state->focus == NULL) {
		return;
	}

	if (*state->focus == 0U) {
		*state->env_voice_on = !*state->env_voice_on;
		state->save_voice_settings();
	} else if (*state->focus == 1U) {
		ui_actions_play_env_voice_test();
	} else if (*state->focus == 2U) {
		static const uint8_t values[] = { 5, 10, 30 };
		(void)ui_settings_cycle_u8(state->env_sample_s, values, sizeof(values) / sizeof(values[0]), 10);
		state->save_settings();
	} else if (*state->focus == 3U) {
		static const int16_t values[] = { 0, 5, 10, 15, 20 };
		(void)ui_settings_cycle_i16(state->env_temp_low_c, values, sizeof(values) / sizeof(values[0]), 10);
		if (*state->env_temp_low_c >= *state->env_temp_high_c) {
			*state->env_temp_high_c = (int16_t)(*state->env_temp_low_c + 5);
		}
		state->save_settings();
	} else if (*state->focus == 4U) {
		static const int16_t values[] = { 25, 30, 35, 40, 45 };
		(void)ui_settings_cycle_i16(state->env_temp_high_c, values, sizeof(values) / sizeof(values[0]), 35);
		if (*state->env_temp_high_c <= *state->env_temp_low_c) {
			*state->env_temp_low_c = (int16_t)(*state->env_temp_high_c - 5);
		}
		state->save_settings();
	} else if (*state->focus == 5U) {
		static const uint16_t values[] = { 20, 30, 40, 50 };
		(void)ui_settings_cycle_u16(state->env_humi_low_percent, values, sizeof(values) / sizeof(values[0]), 30);
		if (*state->env_humi_low_percent >= *state->env_humi_high_percent) {
			*state->env_humi_high_percent = (uint16_t)(*state->env_humi_low_percent + 10U);
		}
		state->save_settings();
	} else if (*state->focus == 6U) {
		static const uint16_t values[] = { 60, 70, 80, 90 };
		(void)ui_settings_cycle_u16(state->env_humi_high_percent, values, sizeof(values) / sizeof(values[0]), 80);
		if (*state->env_humi_high_percent <= *state->env_humi_low_percent) {
			*state->env_humi_low_percent = (uint16_t)(*state->env_humi_high_percent - 10U);
		}
		state->save_settings();
	} else if (*state->focus == 7U) {
		static const uint16_t values[] = { 0, 10, 20, 50, 100 };
		(void)ui_settings_cycle_u16(state->env_lux_low, values, sizeof(values) / sizeof(values[0]), 20);
		if (*state->env_lux_low >= *state->env_lux_high) {
			*state->env_lux_high = (uint16_t)(*state->env_lux_low + 100U);
		}
		state->save_settings();
	} else if (*state->focus == 8U) {
		static const uint16_t values[] = { 300, 500, 800, 1000, 1500 };
		(void)ui_settings_cycle_u16(state->env_lux_high, values, sizeof(values) / sizeof(values[0]), 1000);
		if (*state->env_lux_high <= *state->env_lux_low) {
			*state->env_lux_low = *state->env_lux_high > 100U ? (uint16_t)(*state->env_lux_high - 100U) : 0U;
		}
		state->save_settings();
	} else {
		*state->env_alert_on = !*state->env_alert_on;
		state->save_voice_settings();
	}
}
