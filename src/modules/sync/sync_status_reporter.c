#include <sync_status_reporter.h>

#include <app_bus.h>
#include <app_config.h>
#include <environment_service.h>
#include <presence_service.h>
#include <sync_protocol.h>
#include <sync_transport.h>

#include <esp_log.h>
#include <esp_timer.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "sync_status";

static int64_t s_status_backoff_until_us;
static uint8_t s_status_failures;

static void sync_status_reporter_publish_error(const char *reason)
{
	app_bus_event_t event = {
		.type = APP_BUS_EVENT_SYNC_FAILED,
	};
	strlcpy(event.data.error.reason, reason != NULL ? reason : "sync failed", sizeof(event.data.error.reason));
	(void)app_bus_publish(&event);
}

void sync_status_reporter_report_once(void)
{
	const int64_t now_us = esp_timer_get_time();
	if (now_us < s_status_backoff_until_us || !sync_transport_net_ready()) {
		return;
	}

	app_environment_snapshot_t env = { 0 };
	app_presence_status_t presence = { 0 };
	char sent_at[40] = { 0 };
	(void)environment_service_get_snapshot(&env);
	(void)presence_service_get_status(&presence);
	sync_transport_format_now_iso(sent_at, sizeof(sent_at));

	char *body = sync_protocol_build_status_report(sent_at, &env, &presence);
	if (body == NULL) {
		sync_status_reporter_publish_error("status report oom");
		return;
	}

	char response[64] = { 0 };
	int status = 0;
	int ret = sync_transport_http_request("POST", APP_DEVICE_STATUS_WEB_PATH, body, response, sizeof(response), &status);
	free(body);
	if (ret == 0 && status == 200) {
		s_status_failures = 0;
		s_status_backoff_until_us = 0;
		return;
	}

	s_status_failures++;
	uint32_t backoff_s = sync_backoff_s(s_status_failures, 5U, 60U);
	s_status_backoff_until_us = now_us + (int64_t)backoff_s * 1000000LL;
	ESP_LOGW(TAG, "status report failed ret=%d status=%d backoff=%us", ret, status, (unsigned)backoff_s);
}
