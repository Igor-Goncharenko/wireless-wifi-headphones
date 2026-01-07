#ifndef CONFIG_DISCOVERY_PROTOCOL_H
#define CONFIG_DISCOVERY_PROTOCOL_H

#include <stdint.h>

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

#endif /* CONFIG_DISCOVERY_PROTOCOL_H */
