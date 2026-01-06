#ifndef DISCOVERY_PROTOCOL_H
#define DISCOVERY_PROTOCOL_H

#include <stdint.h>

#define MAX_HEADPHONES_RESPS 16

typedef struct {
    uint16_t sample_rate;
    uint8_t channels;
    uint8_t bit_width;
} headphones_audio_t;

typedef struct {
    char name[32];
    char mac[18];
    char ipv4[16];
    headphones_audio_t audio;
} headphones_info_t;

#endif /* DISCOVERY_PROTOCOL_H */
