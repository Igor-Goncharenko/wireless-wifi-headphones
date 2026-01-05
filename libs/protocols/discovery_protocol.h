#ifndef DISCOVERY_PROTOCOL_H
#define DISCOVERY_PROTOCOL_H

#define MAX_HEADPHONES_RESPS 16

typedef struct {
    char name[32];
    char mac[32];
    char ipv4[16];
} headphones_info_t;

#endif /* DISCOVERY_PROTOCOL_H */
