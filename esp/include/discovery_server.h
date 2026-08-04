#ifndef DISCOVERY_SERVER_H
#define DISCOVERY_SERVER_H

#include "protocols/discovery.h"

extern headphones_info_t g_device_info;

void discovery_start(void);

void discovery_stop(void);

void clear_discovery_before_restart(void);

#endif /* DISCOVERY_SERVER_H */
