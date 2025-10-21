#include "discovery.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/syslog.h>
#include <time.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <syslog.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define MULTICAST_GROUP "224.1.1.1"
#define DISCOVERY_PORT 5000
#define DISCOVERY_TIMEOUT 5
#define BUFFER_SIZE 1024

static int discovery_server_init(discovery_server_t *server) {
    if ((server->sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        return -1;
    }
    
    int reuse = 1;
    if (setsockopt(server->sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt SO_REUSEADDR failed");
        close(server->sockfd);
        return -1;
    }
    
    memset(&server->addr, 0, sizeof(server->addr));
    server->addr.sin_family = AF_INET;
    server->addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server->addr.sin_port = htons(DISCOVERY_PORT);
    
    if (bind(server->sockfd, (struct sockaddr*)&server->addr, sizeof(server->addr)) < 0) {
        perror("bind failed");
        close(server->sockfd);
        return -1;
    }
    
    server->mreq.imr_multiaddr.s_addr = inet_addr(MULTICAST_GROUP);
    server->mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    
    if (setsockopt(server->sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &server->mreq, sizeof(server->mreq)) < 0) {
        perror("setsockopt IP_ADD_MEMBERSHIP failed");
        close(server->sockfd);
        return -1;
    }
    
    struct timeval recv_timeout = {DISCOVERY_TIMEOUT, 0};
    if (setsockopt(server->sockfd, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout)) < 0) {
        perror("setsockopt SO_RCVTIMEO failed");
    }

    return 0;
}

static void discovery_server_destroy(discovery_server_t *server) {
    // close multicast group
    setsockopt(server->sockfd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &server->mreq, sizeof(server->mreq));
    
    // close socket
    if (server->sockfd > 0) {
        close(server->sockfd);
        server->sockfd = -1;
    }
}

static int discover_headphones(const discovery_server_t *server, headphone_response_t *devices, const int max_devices) {
    char buffer[BUFFER_SIZE];
    int device_count = 0;

    struct sockaddr_in multicast_addr;
    memset(&multicast_addr, 0, sizeof(multicast_addr));
    multicast_addr.sin_family = AF_INET;
    multicast_addr.sin_addr.s_addr = inet_addr(MULTICAST_GROUP);
    multicast_addr.sin_port = htons(DISCOVERY_PORT);
    
    const char discovery_msg[] = "DISCOVER_HEADPHONES_REQUEST";
    if (sendto(server->sockfd, discovery_msg, sizeof(discovery_msg) - 1, 0,
               (struct sockaddr*)&multicast_addr, sizeof(multicast_addr)) < 0) {
        perror("sendto failed");
        return -1;
    }
    
    printf("Discovery request sent. Listening for responses...\n");
    
    time_t start_time = time(NULL);
    while ((time(NULL) - start_time) < DISCOVERY_TIMEOUT) {
        struct sockaddr_in sender_addr;
        socklen_t addr_len = sizeof(sender_addr);
        ssize_t recv_len;
        
        recv_len = recvfrom(server->sockfd, buffer, BUFFER_SIZE - 1, 0,
                           (struct sockaddr*)&sender_addr, &addr_len);
        
        if (recv_len > 0) {
            buffer[recv_len] = '\0';
            
            char *type = strstr(buffer, "\"type\":");
            char *model = strstr(buffer, "\"model\":");
            char *id = strstr(buffer, "\"id\":");
            char *ip = strstr(buffer, "\"ip\":");
            
            if (type && model && id && ip) {
                sscanf(type, "\"type\":\"%31[^\"]", devices[device_count].type);
                sscanf(model, "\"model\":\"%63[^\"]", devices[device_count].model);
                sscanf(id, "\"id\":\"%31[^\"]", devices[device_count].id);
                sscanf(ip, "\"ip\":\"%15[^\"]", devices[device_count].ip_v4);
                
                device_count++;
                
                if (device_count >= max_devices) break;
            }
        }
    }
    
    return device_count;
}

int discovery_data_init(discovery_data_t *data) {
    if (pthread_mutex_init(&data->mutex, NULL) != 0) {
        syslog(LOG_ERR, "discovery data mutex init failed");
        return -1;
    }

    data->is_discovering = false;
    data->count = 0;

    return 0;
}

void discovery_data_destroy(discovery_data_t *data) {
    pthread_mutex_lock(&data->mutex);
    pthread_mutex_unlock(&data->mutex);
    pthread_mutex_destroy(&data->mutex);
    data->count = 0;
    data->is_discovering = false;
}

int discover_task(discovery_data_t *data) {
    if (data->is_discovering) {
        syslog(LOG_WARNING, "Cannot start new discovery server while previous did not stop");
        return -1;
    }

    discovery_server_t server = { 0 };
    headphone_response_t hps[16];

    pthread_mutex_lock(&data->mutex);
    
    data->is_discovering = true;

    if (discovery_server_init(&server) != 0) {
        syslog(LOG_ERR, "Failed to init discovery server. errno=%d, strerror=%s\n",
               errno, strerror(errno));
        discovery_server_destroy(&server);
        pthread_mutex_unlock(&data->mutex);
        return -1;
    }

    data->count = discover_headphones(&server, hps, 16);
    memcpy(data->data, hps, data->count * sizeof(headphone_response_t));
    data->is_discovering = false;

    pthread_mutex_unlock(&data->mutex);

    discovery_server_destroy(&server);

    return 0;
}
