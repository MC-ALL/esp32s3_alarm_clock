#include <sync_event_reporter.h>

#include <app_config.h>
#include <environment_service.h>
#include <presence_service.h>
#include <sync_protocol.h>
#include <sync_transport.h>

#include <esp_timer.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SYNC_EVENT_RETRY_DEPTH 8U

typedef struct {
	char event_type[32];
	char todo_id[24];
	uint8_t attempts;
	int64_t next_due_us;
} sync_event_retry_t;

static sync_event_retry_t s_event_retries[SYNC_EVENT_RETRY_DEPTH];
static uint8_t s_event_retry_count;

static int sync_event_reporter_http(const char *event_type, const char *todo_id)
{
	if (event_type == NULL || event_type[0] == '\0' || !sync_transport_net_ready()) {
		return -1;
	}

	app_environment_snapshot_t env = { 0 };
	app_presence_status_t presence = { 0 };
	char event_at[40] = { 0 };
	(void)environment_service_get_snapshot(&env);
	(void)presence_service_get_status(&presence);
	sync_transport_format_now_iso(event_at, sizeof(event_at));

	char *body = sync_protocol_build_event_report(event_at, event_type, todo_id, &env, &presence);
	if (body == NULL) {
		return -1;
	}

	char response[64] = { 0 };
	int status = 0;
	int ret = sync_transport_http_request("POST", APP_DEVICE_EVENTS_WEB_PATH, body, response, sizeof(response), &status);
	free(body);
	return ret == 0 && status == 200 ? 0 : -1;
}

static void sync_event_reporter_retry_add(const char *event_type, const char *todo_id, uint8_t attempts)
{
	if (event_type == NULL || event_type[0] == '\0') {
		return;
	}
	if (s_event_retry_count >= SYNC_EVENT_RETRY_DEPTH) {
		memmove(&s_event_retries[0], &s_event_retries[1], sizeof(s_event_retries[0]) * (SYNC_EVENT_RETRY_DEPTH - 1U));
		s_event_retry_count = SYNC_EVENT_RETRY_DEPTH - 1U;
	}

	sync_event_retry_t *slot = &s_event_retries[s_event_retry_count++];
	memset(slot, 0, sizeof(*slot));
	strlcpy(slot->event_type, event_type, sizeof(slot->event_type));
	if (todo_id != NULL) {
		strlcpy(slot->todo_id, todo_id, sizeof(slot->todo_id));
	}
	slot->attempts = attempts;
	slot->next_due_us = esp_timer_get_time() + (int64_t)sync_backoff_s(attempts + 1U, 5U, 300U) * 1000000LL;
}

void sync_event_reporter_report_once(const char *event_type, const char *todo_id)
{
	if (sync_event_reporter_http(event_type, todo_id) != 0) {
		sync_event_reporter_retry_add(event_type, todo_id, 0);
	}
}

void sync_event_reporter_process_retries(void)
{
	const int64_t now_us = esp_timer_get_time();
	for (uint8_t i = 0; i < s_event_retry_count;) {
		sync_event_retry_t *retry = &s_event_retries[i];
		if (now_us < retry->next_due_us) {
			i++;
			continue;
		}
		if (sync_event_reporter_http(retry->event_type, retry->todo_id) == 0) {
			memmove(retry, retry + 1, sizeof(*retry) * (s_event_retry_count - i - 1U));
			s_event_retry_count--;
			continue;
		}
		retry->attempts++;
		retry->next_due_us = now_us + (int64_t)sync_backoff_s(retry->attempts, 5U, 300U) * 1000000LL;
		i++;
	}
}
