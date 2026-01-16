#ifndef RTP_SERVER_H
#define RTP_SERVER_H

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "lwip/sockets.h"
#include <stdint.h>

typedef struct {
    int sockfd;
    struct sockaddr_in addr;
    
    uint16_t expected_sequence;
    uint32_t packets_received;
    uint32_t packets_lost;

    const RingbufHandle_t *rb;
} rtp_server_t;

void rtp_server_mgr_task(void *arg);

#endif /* RTP_SERVER_H */
