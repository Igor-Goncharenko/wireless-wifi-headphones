#ifndef WIFI_H
#define WIFI_H

#include <pthread.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define MAX_HEADPHONES_RESPS 16

typedef struct {
    int sockfd;
    struct sockaddr_in addr;
    struct ip_mreq mreq;
} discovery_server_t;

typedef struct {
    char type[32];
    char model[64];
    char id[32];
    char ip_v4[16];
} headphone_response_t;

typedef struct {
    headphone_response_t data[MAX_HEADPHONES_RESPS];
    int count;
    bool is_discovering;
    pthread_mutex_t mutex;
} discovery_data_t;

int discovery_data_init(discovery_data_t *data);
void discovery_data_destroy(discovery_data_t *data);
int discover_task(discovery_data_t *data);

#endif /* WIFI_H */
