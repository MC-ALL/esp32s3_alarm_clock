#ifndef REMINDER_EVENTS_H_
#define REMINDER_EVENTS_H_

#include <audio_service.h>
#include <settings_model.h>

void reminder_events_publish_device(const char *event_type, const char *todo_id);
void reminder_events_publish_alarm_settings_changed(const app_settings_t *settings);
int reminder_events_request_audio(app_audio_event_t event_id);

#endif
