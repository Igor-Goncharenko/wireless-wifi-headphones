#ifndef CONFIG_HEADPHONES_H
#define CONFIG_HEADPHONES_H

#include <stdint.h>

/* command types */
typedef enum {
    HPCMD_NO_COMMAND = 0x00,
    HPCMD_PING = 0x01,
    HPCMD_DISCONNECT = 0x02,
} headphones_command_types_e;

typedef struct {
    uint8_t command;
    uint32_t timestamp;
    uint16_t sequence;
} headphones_packet_t;

#endif /* CONFIG_HEADPHONES_H */
