#ifndef NET_TIME_H_
#define NET_TIME_H_

#include <stdbool.h>

void net_time_configure_timezone(void);
void net_time_start_sntp(bool time_synced);
void net_time_stop_sntp(void);
void net_time_try_mark_synced(bool *time_synced);

#endif
