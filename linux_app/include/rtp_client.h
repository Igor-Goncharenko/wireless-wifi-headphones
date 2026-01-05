#ifndef RTP_CLIENT_H
#define RTP_CLIENT_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <arpa/inet.h>

#include "ringbuf.h"

#define RTP_PORT 5002
#define RTP_VERSION 2
#define RTP_PAYLOAD_TYPE 96  // Dynamic payload type for PCM
#define AUDIO_SAMPLE_RATE 44100
#define CHANNELS 2
#define SAMPLE_SIZE 2  // 16-bit PCM

#define FRAMES_PER_PACKET 1024
#define PACKET_SIZE (FRAMES_PER_PACKET + sizeof(rtp_header_t))

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
