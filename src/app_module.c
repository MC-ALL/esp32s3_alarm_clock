#include "app_module.h"
#include <module_common.h>

#include <esp_log.h>

static const char *TAG = "app_module";

static const struct app_module_desc MODULES[] = {
	{ .name = "fault_state", .init = fault_state_init, .start = fault_state_start, .stop = fault_state_stop },
	{ .name = "persistence_broker", .init = persistence_broker_init, .start = persistence_broker_start, .stop = persistence_broker_stop },
	{ .name = "app_bus", .init = app_bus_init, .start = app_bus_start, .stop = app_bus_stop },
	{ .name = "settings_model", .init = settings_model_init, .start = settings_model_start, .stop = settings_model_stop },
	{ .name = "timebase_service", .init = timebase_service_init, .start = timebase_service_start, .stop = timebase_service_stop },
	{ .name = "display_service", .init = display_service_init, .start = display_service_start, .stop = display_service_stop },
	{ .name = "backlight_service", .init = backlight_service_init, .start = backlight_service_start, .stop = backlight_service_stop },
	{ .name = "input_service", .init = input_service_init, .start = input_service_start, .stop = input_service_stop },
	{ .name = "ui_model", .init = ui_model_init, .start = ui_model_start, .stop = ui_model_stop },
	{ .name = "environment_service", .init = environment_service_init, .start = environment_service_start, .stop = environment_service_stop },
	{ .name = "presence_service", .init = presence_service_init, .start = presence_service_start, .stop = presence_service_stop },
	{ .name = "audio_service", .init = audio_service_init, .start = audio_service_start, .stop = audio_service_stop },
	{ .name = "net_service", .init = net_service_init, .start = net_service_start, .stop = net_service_stop },
	{ .name = "sync_service", .init = sync_service_init, .start = sync_service_start, .stop = sync_service_stop },
	{ .name = "reminder_service", .init = reminder_service_init, .start = reminder_service_start, .stop = reminder_service_stop },
	{ .name = "lifecycle_service", .init = lifecycle_service_init, .start = lifecycle_service_start, .stop = lifecycle_service_stop },
};

const struct app_module_desc *app_modules_get(size_t *count)
{
	if (count != NULL) {
		*count = sizeof(MODULES) / sizeof(MODULES[0]);
	}

	return MODULES;
}

static void app_modules_stop_range(const struct app_module_desc *modules, size_t count)
{
	for (size_t i = count; i > 0; i--) {
		const struct app_module_desc *module = &modules[i - 1];
		if (module->stop == NULL) {
			continue;
		}

		int ret = module->stop();
		if (ret != 0) {
			ESP_LOGE(TAG, "module %s stop failed during rollback: %d", module->name, ret);
		}
	}
}

int app_modules_init_all(void)
{
	size_t count = 0;
	const struct app_module_desc *modules = app_modules_get(&count);

	for (size_t i = 0; i < count; i++) {
		const struct app_module_desc *module = &modules[i];
		if (module->init == NULL) {
			continue;
		}

		int ret = module->init();
		if (ret != 0) {
			ESP_LOGE(TAG, "module %s init failed: %d", module->name, ret);
			app_modules_stop_range(modules, i);
			return ret;
		}
	}

	return 0;
}

int app_modules_start_all(void)
{
	size_t count = 0;
	const struct app_module_desc *modules = app_modules_get(&count);

	for (size_t i = 0; i < count; i++) {
		const struct app_module_desc *module = &modules[i];
		if (module->start == NULL) {
			continue;
		}

		int ret = module->start();
		if (ret != 0) {
			ESP_LOGE(TAG, "module %s start failed: %d", module->name, ret);
			app_modules_stop_range(modules, i + 1);
			return ret;
		}
	}

	return 0;
}

int app_modules_stop_all(void)
{
	size_t count = 0;
	const struct app_module_desc *modules = app_modules_get(&count);
	int first_error = 0;

	for (size_t i = count; i > 0; i--) {
		const struct app_module_desc *module = &modules[i - 1];
		if (module->stop == NULL) {
			continue;
		}

		int ret = module->stop();
		if (ret != 0) {
			ESP_LOGE(TAG, "module %s stop failed: %d", module->name, ret);
			if (first_error == 0) {
				first_error = ret;
			}
		}
	}

	return first_error;
}
