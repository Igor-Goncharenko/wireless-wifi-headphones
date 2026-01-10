#include "discovery.h"

#include <errno.h>
#include <pthread.h>
#include <sys/syslog.h>
#include <time.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <syslog.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "config.h"

static int discovery_server_init(discovery_server_t *server) {
    if ((server->sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        syslog(LOG_ERR, "Failed to create discovery socket: errno=%d, strerror=\"%s\"",
               errno, strerror(errno));
        return -1;
    }
    
    int reuse = 1;
    if (setsockopt(server->sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        syslog(LOG_ERR, "discovery server setsockopt SO_REUSEADDR failed: errno=%d, strerror=\"%s\"",
               errno, strerror(errno));
        close(server->sockfd);
        return -1;
    }
    
    memset(&server->addr, 0, sizeof(server->addr));
    server->addr.sin_family = AF_INET;
    server->addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server->addr.sin_port = htons(DISCOVERY_PORT);
    
    if (bind(server->sockfd, (struct sockaddr*)&server->addr, sizeof(server->addr)) < 0) {
        syslog(LOG_ERR, "Failed to bind discovery socket: errno=%d, strerror=\"%s\"",
               errno, strerror(errno));
        close(server->sockfd);
        return -1;
    }
    
    server->mreq.imr_multiaddr.s_addr = inet_addr(MULTICAST_GROUP);
    server->mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    
    if (setsockopt(server->sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &server->mreq, sizeof(server->mreq)) < 0) {
        syslog(LOG_ERR, "Discovery server setsockopt IP_ADD_MEMBERSHIP failed: errno=%d, strerror=\"%s\"",
               errno, strerror(errno));
        close(server->sockfd);
        return -1;
    }
    
    struct timeval recv_timeout = {MAX_DISCOVERY_DURATION, 0};
    if (setsockopt(server->sockfd, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout)) < 0) {
        syslog(LOG_ERR, "Discovery server setsockopt SO_RCVTIMEO failed: errno=%d, strerror=\"%s\"",
               errno, strerror(errno));
    }

    unsigned char loopback = 0;
    if (setsockopt(server->sockfd, IPPROTO_IP, IP_MULTICAST_LOOP, 
                &loopback, sizeof(loopback)) < 0) {
        syslog(LOG_ERR, "Discovery server setsockopt IP_MULTICAST_LOOP failed: errno=%d, strerror=\"%s\"",
               errno, strerror(errno));
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

static int discover_headphones(const discovery_server_t *server, headphones_info_t *devices,
                               const int max_devices, const int duration) {
    int device_count = 0;

    struct sockaddr_in multicast_addr;
    memset(&multicast_addr, 0, sizeof(multicast_addr));
    multicast_addr.sin_family = AF_INET;
    multicast_addr.sin_addr.s_addr = inet_addr(MULTICAST_GROUP);
    multicast_addr.sin_port = htons(DISCOVERY_PORT);

    if (sendto(server->sockfd, DISCOVERY_REQUEST, sizeof(DISCOVERY_REQUEST) - 1, 0,
               (struct sockaddr*)&multicast_addr, sizeof(multicast_addr)) < 0) {
        syslog(LOG_ERR, "Discovery server sendto failed: errno=%d, strerror=\"%s\"",
               errno, strerror(errno));
        return -1;
    }

    syslog(LOG_INFO, "Discovery request sent. Listening for responses...");

    time_t start_time = time(NULL);
    while ((time(NULL) - start_time) < duration && device_count < max_devices) {
        struct sockaddr_in sender_addr;
        socklen_t addr_len = sizeof(sender_addr);
        ssize_t recv_len;

        headphones_info_t received_dev;
        recv_len = recvfrom(server->sockfd, &received_dev, sizeof(headphones_info_t), 0,
                            (struct sockaddr*)&sender_addr, &addr_len);

        if (recv_len != sizeof(headphones_info_t)) {
            syslog(LOG_ERR, "Failed to recv discovery data, received=%zd (exp=%zu)", recv_len,
                   sizeof(headphones_info_t));
            continue;
        }

        memcpy(&devices[device_count], &received_dev, sizeof(headphones_info_t));
        devices[device_count].audio.sample_rate = ntohs(received_dev.audio.sample_rate);

        device_count++;
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

int discover_task(discovery_data_t *data, const int duration) {
    if (data->is_discovering) {
        syslog(LOG_WARNING, "Cannot start new discovery server while previous did not stop");
        return -1;
    }

    discovery_server_t server = { 0 };
    headphones_info_t hps[16];

    pthread_mutex_lock(&data->mutex);
    
    data->is_discovering = true;

    if (discovery_server_init(&server) != 0) {
        syslog(LOG_ERR, "Failed to init discovery server. errno=%d, strerror=%s",
               errno, strerror(errno));
        discovery_server_destroy(&server);
        pthread_mutex_unlock(&data->mutex);
        return -1;
    }

    data->count = discover_headphones(&server, hps, 16, duration);
    memcpy(data->data, hps, data->count * sizeof(headphones_info_t));
    data->is_discovering = false;

    pthread_mutex_unlock(&data->mutex);

    discovery_server_destroy(&server);

    return 0;
}

static bool check_ip_exist(const char ip4[16], const discovery_data_t *data) {
    for (int i = 0; i < data->count; i++) {
        if (strcmp(data->data[i].ipv4, ip4) == 0) {
            return true;
        }
    }
    return false;
}

bool handshake(const char ip4[16], const discovery_data_t *data) {
    if (!check_ip_exist(ip4, data)) {
        syslog(LOG_WARNING, "Trying to connect to a non-existent IP");
        return false;
    }

    int sockfd = 0;
    struct sockaddr_in serv_addr;
    char buffer[128];

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        syslog(LOG_ERR, "Socket creation error");
        return false;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(HANDSHAKE_PORT);

    if (inet_pton(AF_INET, ip4, &serv_addr.sin_addr) <= 0) {
        syslog(LOG_ERR, "Invalid address/Address not supported");
        close(sockfd);
        return false;
    }

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        syslog(LOG_ERR, "Connection Failed");
        close(sockfd);
        return false;
    }

    int bytes_sent = send(sockfd, HANDSHAKE_REQUEST, sizeof(HANDSHAKE_REQUEST), 0);
    if (bytes_sent < 0) {
        syslog(LOG_ERR, "Send failed");
        return false;
    }

    int bytes_received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received < 0) {
        syslog(LOG_ERR, "Receive failed");
        return false;
    }
    buffer[bytes_received] = '\0';

    close(sockfd);
    return strcmp(buffer, HANDSHAKE_RESPONSE) == 0;
}
