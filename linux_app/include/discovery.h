#ifndef WIFI_H
#define WIFI_H

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

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

int discovery_server_init(discovery_server_t *server);
void discovery_server_destroy(discovery_server_t *server);
int discover_headphones(const discovery_server_t *server, headphone_response_t *devices, const int max_devices);

#endif /* WIFI_H */
