#ifndef WIFI_H
#define WIFI_H

#include <pthread.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "discovery_protocol.h"

#define MAX_DISCOVERY_DURATION 10

typedef struct {
    int sockfd;
    struct sockaddr_in addr;
    struct ip_mreq mreq;
} discovery_server_t;

typedef struct {
    headphones_info_t data[MAX_HEADPHONES_RESPS];
    int count;
    bool is_discovering;
    pthread_mutex_t mutex;
} discovery_data_t;

int discovery_data_init(discovery_data_t *data);
void discovery_data_destroy(discovery_data_t *data);
int discover_task(discovery_data_t *data, const int duration);

#endif /* WIFI_H */
