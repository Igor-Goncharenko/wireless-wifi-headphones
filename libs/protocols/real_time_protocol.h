#ifndef REAL_TIME_PROTOCOL_H
#define REAL_TIME_PROTOCOL_H

#include <stdint.h>

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
} __attribute__((packed)) rtp_header_t;

#endif /* REAL_TIME_PROTOCOL_H */
