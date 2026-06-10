#ifndef SYNC_PERIODIC_TIMER_H_
#define SYNC_PERIODIC_TIMER_H_

#include <sync/sync_request_retry.h>

typedef void (*sync_periodic_timer_request_fn_t)(const sync_request_t *request, void *ctx);

typedef struct {
	sync_periodic_timer_request_fn_t queue_request;
	void *ctx;
} sync_periodic_timer_config_t;

typedef struct {
	void *config_timer;
	void *status_timer;
	bool config_timer_running;
	bool status_timer_running;
	int64_t last_config_pull_us;
	int64_t last_status_report_us;
	sync_periodic_timer_config_t config;
} sync_periodic_timer_t;

int sync_periodic_timer_init(sync_periodic_timer_t *timer, const sync_periodic_timer_config_t *config);
void sync_periodic_timer_start(sync_periodic_timer_t *timer);
void sync_periodic_timer_stop(sync_periodic_timer_t *timer);
void sync_periodic_timer_deinit(sync_periodic_timer_t *timer);

#endif
