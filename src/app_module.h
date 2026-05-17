#ifndef APP_MODULE_H_
#define APP_MODULE_H_

#include <stddef.h>

struct app_module_desc {
	const char *name;
	int (*init)(void);
	int (*start)(void);
	int (*stop)(void);
};

const struct app_module_desc *app_modules_get(size_t *count);
int app_modules_init_all(void);
int app_modules_start_all(void);
int app_modules_stop_all(void);

#endif
