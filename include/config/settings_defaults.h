#ifndef SETTINGS_DEFAULTS_H_
#define SETTINGS_DEFAULTS_H_

#include <config/settings_model.h>

void settings_defaults_apply(app_settings_t *settings);
void settings_defaults_sanitize(app_settings_t *settings);

#endif
