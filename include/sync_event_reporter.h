#ifndef SYNC_EVENT_REPORTER_H_
#define SYNC_EVENT_REPORTER_H_

void sync_event_reporter_report_once(const char *event_type, const char *todo_id);
void sync_event_reporter_process_retries(void);

#endif
