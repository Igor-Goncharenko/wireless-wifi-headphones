#ifndef DISCOVERY_SERVER_H
#define DISCOVERY_SERVER_H

typedef struct {
    char model[32];
    char device_id[32];
    char ip_addr[16];
} device_info_t;

extern device_info_t g_device_info;

void discovery_server_task(void *args);

#endif /* DISCOVERY_SERVER_H */
