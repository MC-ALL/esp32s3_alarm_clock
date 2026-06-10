#include <sync/sync_periodic_timer.h>

#include <core/app_config.h>

#include <esp_timer.h>
#include <stddef.h>

static void sync_periodic_timer_queue(sync_periodic_timer_t *timer, sync_request_type_t type)
{
	if (timer == NULL || timer->config.queue_request == NULL) {
		return;
	}
	timer->config.queue_request(&(sync_request_t){ .type = type }, timer->config.ctx);
}

static void sync_config_timer_cb(void *arg)
{
	sync_periodic_timer_t *timer = (sync_periodic_timer_t *)arg;
	if (timer == NULL) {
		return;
	}
	const int64_t now_us = esp_timer_get_time();
	if (timer->last_config_pull_us > 0 &&
	    (now_us - timer->last_config_pull_us) < (int64_t)APP_CONFIG_SYNC_INTERVAL_S * 1000000LL) {
		return;
	}
	timer->last_config_pull_us = now_us;
	sync_periodic_timer_queue(timer, SYNC_REQUEST_PULL_CONFIG);
}

static void sync_status_timer_cb(void *arg)
{
	sync_periodic_timer_t *timer = (sync_periodic_timer_t *)arg;
	if (timer == NULL) {
		return;
	}
	const int64_t now_us = esp_timer_get_time();
	if (timer->last_status_report_us > 0 &&
	    (now_us - timer->last_status_report_us) < (int64_t)APP_STATUS_REPORT_INTERVAL_S * 1000000LL) {
		return;
	}
	timer->last_status_report_us = now_us;
	sync_periodic_timer_queue(timer, SYNC_REQUEST_REPORT_STATUS);
}

int sync_periodic_timer_init(sync_periodic_timer_t *timer, const sync_periodic_timer_config_t *config)
{
	if (timer == NULL || config == NULL || config->queue_request == NULL) {
		return -1;
	}

	*timer = (sync_periodic_timer_t){
		.config = *config,
	};

	const esp_timer_create_args_t config_timer_args = {
		.callback = sync_config_timer_cb,
		.arg = timer,
		.name = "config_sync",
	};
	esp_timer_handle_t config_timer = NULL;
	if (esp_timer_create(&config_timer_args, &config_timer) != ESP_OK) {
		return -1;
	}
	timer->config_timer = config_timer;

	const esp_timer_create_args_t status_timer_args = {
		.callback = sync_status_timer_cb,
		.arg = timer,
		.name = "status_report",
	};
	esp_timer_handle_t status_timer = NULL;
	if (esp_timer_create(&status_timer_args, &status_timer) != ESP_OK) {
		(void)esp_timer_delete((esp_timer_handle_t)timer->config_timer);
		timer->config_timer = NULL;
		return -1;
	}
	timer->status_timer = status_timer;
	return 0;
}

void sync_periodic_timer_start(sync_periodic_timer_t *timer)
{
	if (timer == NULL) {
		return;
	}
	if (timer->config_timer != NULL && !timer->config_timer_running &&
	    esp_timer_start_periodic((esp_timer_handle_t)timer->config_timer,
				     (uint64_t)APP_CONFIG_SYNC_INTERVAL_S * 1000000ULL) == ESP_OK) {
		timer->config_timer_running = true;
	}
	if (timer->status_timer != NULL && !timer->status_timer_running &&
	    esp_timer_start_periodic((esp_timer_handle_t)timer->status_timer,
				     (uint64_t)APP_STATUS_REPORT_INTERVAL_S * 1000000ULL) == ESP_OK) {
		timer->status_timer_running = true;
	}
}

void sync_periodic_timer_stop(sync_periodic_timer_t *timer)
{
	if (timer == NULL) {
		return;
	}
	if (timer->config_timer != NULL && timer->config_timer_running) {
		(void)esp_timer_stop((esp_timer_handle_t)timer->config_timer);
		timer->config_timer_running = false;
	}
	if (timer->status_timer != NULL && timer->status_timer_running) {
		(void)esp_timer_stop((esp_timer_handle_t)timer->status_timer);
		timer->status_timer_running = false;
	}
}

void sync_periodic_timer_deinit(sync_periodic_timer_t *timer)
{
	if (timer == NULL) {
		return;
	}
	sync_periodic_timer_stop(timer);
	if (timer->config_timer != NULL) {
		(void)esp_timer_delete((esp_timer_handle_t)timer->config_timer);
		timer->config_timer = NULL;
	}
	if (timer->status_timer != NULL) {
		(void)esp_timer_delete((esp_timer_handle_t)timer->status_timer);
		timer->status_timer = NULL;
	}
}
