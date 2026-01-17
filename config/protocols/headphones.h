#ifndef CONFIG_HEADPHONES_H
#define CONFIG_HEADPHONES_H

#include <stdint.h>

/* command types */
#define HP_PING_REQUEST 0x01
#define HP_PING_RESPONSE 0x02
#define HP_DISCONNECT 0x03

typedef struct {
    uint8_t command;
    uint32_t timestamp;
    uint16_t sequence;
} headphones_packet_t;

#endif /* CONFIG_HEADPHONES_H */
