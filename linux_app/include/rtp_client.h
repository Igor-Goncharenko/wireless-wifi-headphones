#ifndef RTP_CLIENT_H
#define RTP_CLIENT_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <arpa/inet.h>

#include "ringbuf.h"

typedef struct {
    int sockfd;
    struct sockaddr_in server_addr;
    uint16_t sequence;
    uint32_t timestamp;
    uint32_t ssrc;
} rtp_session_t;

typedef struct {
    bool has_active_session;
    rtp_session_t active_session;
    pthread_t tid;

    char ipv4[16];

    pthread_mutex_t mutex;
    bool is_running;

    ringbuf_t *rb_ptr;
} rtp_connection_data_t;

int rtp_connection_data_init(rtp_connection_data_t *data, ringbuf_t *rb_ptr);

void rtp_connection_data_destroy(rtp_connection_data_t *data);

int rtp_connection_start(rtp_connection_data_t *data, const char ip4[16]);

void rtp_connection_stop(rtp_connection_data_t *data);

#endif /* RTP_CLIENT_H */
