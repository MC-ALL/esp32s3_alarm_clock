#ifndef APP_MODULE_COMMON_H_
#define APP_MODULE_COMMON_H_

int lifecycle_service_init(void);
int lifecycle_service_start(void);
int lifecycle_service_stop(void);

int fault_state_init(void);
int fault_state_start(void);
int fault_state_stop(void);

int timebase_service_init(void);
int timebase_service_start(void);
int timebase_service_stop(void);

int app_bus_init(void);
int app_bus_start(void);
int app_bus_stop(void);

int settings_model_init(void);
int settings_model_start(void);
int settings_model_stop(void);

int persistence_broker_init(void);
int persistence_broker_start(void);
int persistence_broker_stop(void);

int input_service_init(void);
int input_service_start(void);
int input_service_stop(void);

int ui_model_init(void);
int ui_model_start(void);
int ui_model_stop(void);

int display_service_init(void);
int display_service_start(void);
int display_service_stop(void);

int backlight_service_init(void);
int backlight_service_start(void);
int backlight_service_stop(void);

int environment_service_init(void);
int environment_service_start(void);
int environment_service_stop(void);

int presence_service_init(void);
int presence_service_start(void);
int presence_service_stop(void);

int net_service_init(void);
int net_service_start(void);
int net_service_stop(void);

int sync_service_init(void);
int sync_service_start(void);
int sync_service_stop(void);

int reminder_service_init(void);
int reminder_service_start(void);
int reminder_service_stop(void);

int audio_service_init(void);
int audio_service_start(void);
int audio_service_stop(void);

#endif
