#ifndef DISCOVERY_SERVER_H
#define DISCOVERY_SERVER_H

#include <stdint.h>

#include "protocols/discovery.h"

extern headphones_info_t g_device_info;

void discovery_server_mgr_task(void *arg);

#endif /* DISCOVERY_SERVER_H */
