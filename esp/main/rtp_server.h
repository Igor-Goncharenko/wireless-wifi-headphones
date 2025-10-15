#ifndef RTP_SERVER_H
#define RTP_SERVER_H

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "lwip/sockets.h"
#include <stdint.h>

typedef struct {
    int sockfd;
    struct sockaddr_in server_addr;
    
    uint16_t expected_sequence;
    uint32_t packets_received;
    uint32_t packets_lost;

    RingbufHandle_t rb;
} rtp_server_t;

#pragma pack(push, 1)
typedef struct {
    // first byte
    uint8_t contributor_count : 4;
    uint8_t ver : 2;
    uint8_t p : 1;
    uint8_t x : 1;

    // second byte
    uint8_t payload_types : 7;
    uint8_t m : 1;

    // other
    uint16_t sequence;
    uint32_t timestamp;
    uint32_t ssrc;
} rtp_header_t;
#pragma pack(pop)

int rtp_server_init(rtp_server_t *rtp_ser, const RingbufHandle_t rb);

void rtp_server_destroy(rtp_server_t *rtp_ser);

void rtp_receiver_task(void *args);

#endif /* RTP_SERVER_H */
