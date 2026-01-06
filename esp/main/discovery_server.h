#ifndef DISCOVERY_SERVER_H
#define DISCOVERY_SERVER_H

#include "discovery_protocol.h"

extern device_info_t g_device_info;

void discovery_server_task(void *args);

#endif /* DISCOVERY_SERVER_H */
