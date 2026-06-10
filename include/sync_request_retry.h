#ifndef SYNC_REQUEST_RETRY_H_
#define SYNC_REQUEST_RETRY_H_

#include <stdbool.h>
#include <stdint.h>

#include <settings_model.h>

#define SYNC_REQUEST_RETRY_DEPTH 8U

typedef enum {
	SYNC_REQUEST_PULL_CONFIG = 1,
	SYNC_REQUEST_REPORT_STATUS,
	SYNC_REQUEST_COMPLETE_TODO,
	SYNC_REQUEST_DELETE_TODO,
	SYNC_REQUEST_PUSH_ALARMS,
	SYNC_REQUEST_PUSH_VOICE,
	SYNC_REQUEST_REPORT_EVENT,
} sync_request_type_t;

typedef struct {
	sync_request_type_t type;
	char todo_id[24];
	char event_type[32];
	app_settings_t settings;
} sync_request_t;

typedef struct {
	sync_request_t request;
	uint8_t attempts;
	int64_t next_due_us;
} sync_request_retry_t;

typedef struct {
	sync_request_retry_t items[SYNC_REQUEST_RETRY_DEPTH];
	uint8_t count;
} sync_request_retry_state_t;

typedef int (*sync_request_execute_fn_t)(const sync_request_t *request);

bool sync_request_retryable(sync_request_type_t type);
void sync_request_retry_add(sync_request_retry_state_t *state, const sync_request_t *request, uint8_t attempts);
void sync_request_retry_process(sync_request_retry_state_t *state, sync_request_execute_fn_t execute_request);

#endif
