#ifndef DISCOVERY_PROTOCOL_H
#define DISCOVERY_PROTOCOL_H

#include <stdint.h>

#define MAX_HEADPHONES_RESPS 16

typedef struct {
    char name[32];
    char mac[18];
    char ipv4[16];
} headphones_info_t;

typedef struct {
    headphones_info_t info;
    uint16_t sample_rate;
    uint8_t channels;
    uint8_t bit_width;
} device_info_t;

#endif /* DISCOVERY_PROTOCOL_H */
