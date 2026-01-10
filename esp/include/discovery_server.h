#ifndef DISCOVERY_SERVER_H
#define DISCOVERY_SERVER_H

#include <stdint.h>
#include <time.h>
#include "lwip/sockets.h"

#include "protocols/discovery.h"

#define MAX_WHITELIST_SIZE 16
#define WHITELIST_TIMEOUT 600   // seconds

typedef struct {
    time_t timestamp;
    struct in_addr addr;
} whitelist_entry_t;

extern headphones_info_t g_device_info;

void discovery_server_mgr_task(void *arg);

#endif /* DISCOVERY_SERVER_H */
