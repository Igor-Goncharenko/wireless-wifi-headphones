#ifndef RTP_CLIENT_H
#define RTP_CLIENT_H

#include <stdint.h>
#include <arpa/inet.h>

#define RTP_PORT 5002
#define RTP_VERSION 2
#define RTP_PAYLOAD_TYPE 96  // Dynamic payload type for PCM
#define AUDIO_SAMPLE_RATE 44100
#define CHANNELS 2
#define SAMPLE_SIZE 2  // 16-bit PCM

#define FRAMES_PER_PACKET 1024
#define MAX_PACKET_SIZE (FRAMES_PER_PACKET + sizeof(rtp_header_t))

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

int rtp_session_create(rtp_session_t *session, const char *server_ip, const int server_port);

ssize_t rtp_send_packet(rtp_session_t *session, const uint8_t *data, const size_t data_size, const int marker);

void rtp_session_destroy(rtp_session_t *session);

#endif /* RTP_CLIENT_H */
