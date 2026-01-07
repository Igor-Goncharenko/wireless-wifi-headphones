#ifndef DISCOVERY_SERVER_H
#define DISCOVERY_SERVER_H

#include "protocols/discovery.h"

extern headphones_info_t g_device_info;

void discovery_server_task(void *args);

#endif /* DISCOVERY_SERVER_H */
