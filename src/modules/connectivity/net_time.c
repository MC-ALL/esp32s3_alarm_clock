#include <net_time.h>

#include <app_config.h>

#include <lwip/apps/sntp.h>
#include <stdlib.h>
#include <time.h>

void net_time_configure_timezone(void)
{
	setenv("TZ", "CST-8", 1);
	tzset();
}

void net_time_start_sntp(bool time_synced)
{
	if (time_synced) {
		return;
	}
	sntp_stop();
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
	sntp_setservername(0, APP_SNTP_SERVER);
	sntp_init();
}

void net_time_stop_sntp(void)
{
	sntp_stop();
}

void net_time_try_mark_synced(bool *time_synced)
{
	if (time_synced == NULL) {
		return;
	}

	time_t now = 0;
	struct tm timeinfo = { 0 };

	time(&now);
	localtime_r(&now, &timeinfo);
	if (timeinfo.tm_year > (2016 - 1900)) {
		*time_synced = true;
	}
}
