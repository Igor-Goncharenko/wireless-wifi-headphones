#ifndef COMMANDS_H
#define COMMANDS_H

#include "lwip/sockets.h"
#include <stdint.h>

typedef struct {
    int sockfd;
    struct sockaddr_in addr;

    uint32_t expected_sequence;
    uint32_t packets_received;
    uint32_t packets_lost;

    uint32_t sender_seq;

    struct in_addr allowed_ip4;
} commands_server_t;

int push_command(uint8_t command_type);

void commands_server_mgr_task(void *arg);

void clear_commands_sock_before_restart(void);

#endif /* COMMANDS_H */
