#include <sync/sync_request_retry.h>
#include <sync/sync_transport.h>

#include <esp_timer.h>
#include <string.h>

bool sync_request_retryable(sync_request_type_t type)
{
	return type == SYNC_REQUEST_COMPLETE_TODO || type == SYNC_REQUEST_DELETE_TODO ||
	       type == SYNC_REQUEST_PUSH_ALARMS || type == SYNC_REQUEST_PUSH_VOICE;
}

void sync_request_retry_add(sync_request_retry_state_t *state, const sync_request_t *request, uint8_t attempts)
{
	if (state == NULL || request == NULL) {
		return;
	}

	if (request->type == SYNC_REQUEST_PUSH_ALARMS || request->type == SYNC_REQUEST_PUSH_VOICE) {
		for (uint8_t i = 0; i < state->count; i++) {
			if (state->items[i].request.type == request->type) {
				state->items[i].request = *request;
				state->items[i].attempts = attempts;
				state->items[i].next_due_us =
					esp_timer_get_time() +
					(int64_t)sync_backoff_s(attempts + 1U, 5U, 300U) * 1000000LL;
				return;
			}
		}
	}

	if (state->count >= SYNC_REQUEST_RETRY_DEPTH) {
		memmove(&state->items[0], &state->items[1],
			sizeof(state->items[0]) * (SYNC_REQUEST_RETRY_DEPTH - 1U));
		state->count = SYNC_REQUEST_RETRY_DEPTH - 1U;
	}

	sync_request_retry_t *slot = &state->items[state->count++];
	memset(slot, 0, sizeof(*slot));
	slot->request = *request;
	slot->attempts = attempts;
	slot->next_due_us =
		esp_timer_get_time() + (int64_t)sync_backoff_s(attempts + 1U, 5U, 300U) * 1000000LL;
}

void sync_request_retry_process(sync_request_retry_state_t *state, sync_request_execute_fn_t execute_request)
{
	if (state == NULL || execute_request == NULL) {
		return;
	}

	const int64_t now_us = esp_timer_get_time();
	for (uint8_t i = 0; i < state->count;) {
		sync_request_retry_t *retry = &state->items[i];
		if (now_us < retry->next_due_us) {
			i++;
			continue;
		}
		if (execute_request(&retry->request) == 0) {
			memmove(retry, retry + 1, sizeof(*retry) * (state->count - i - 1U));
			state->count--;
			continue;
		}
		retry->attempts++;
		retry->next_due_us = now_us + (int64_t)sync_backoff_s(retry->attempts, 5U, 300U) * 1000000LL;
		i++;
	}
}
