#ifndef UI_ACTIONS_H_
#define UI_ACTIONS_H_

#include <core/app_bus.h>
#include <config/settings_model.h>

void ui_actions_publish_settings(app_bus_event_type_t type, const app_settings_t *settings);
int ui_actions_request_todo_sync(void);
int ui_actions_publish_todo(app_bus_event_type_t type, const char *todo_id);
int ui_actions_publish_device_event(const char *event_type, const char *todo_id);
void ui_actions_play_alarm_voice_test(void);
void ui_actions_play_todo_voice_test(void);
void ui_actions_play_env_voice_test(void);

#endif
