#ifndef SETTINGS_STORAGE_H_
#define SETTINGS_STORAGE_H_

#include <stdbool.h>

#include <config/settings_model.h>

typedef enum {
	SETTINGS_STORAGE_LOAD_OK = 0,
	SETTINGS_STORAGE_LOAD_MISSING,
	SETTINGS_STORAGE_LOAD_INCOMPATIBLE,
	SETTINGS_STORAGE_LOAD_ERROR,
} settings_storage_load_result_t;

settings_storage_load_result_t settings_storage_load(app_settings_t *settings);
int settings_storage_save(const app_settings_t *settings);

#endif
