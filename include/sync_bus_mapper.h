#ifndef SYNC_BUS_MAPPER_H_
#define SYNC_BUS_MAPPER_H_

#include <stdbool.h>

#include <app_bus.h>
#include <sync_request_retry.h>

bool sync_bus_mapper_event_to_request(const app_bus_event_t *event, sync_request_t *request);

#endif
