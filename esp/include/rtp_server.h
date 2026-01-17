#ifndef RTP_SERVER_H
#define RTP_SERVER_H

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "lwip/sockets.h"
#include <stdint.h>

typedef struct {
    int sockfd;
    struct sockaddr_in addr;
    struct in_addr allowed_ip4;
    
    uint16_t expected_sequence;
    uint32_t packets_received;
    uint32_t packets_lost;

    const RingbufHandle_t *rb;
} rtp_server_t;

void rtp_server_mgr_task(void *arg);

void clear_rtp_sock_before_restart(void);

#endif /* RTP_SERVER_H */
