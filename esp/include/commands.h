#ifndef COMMANDS_H
#define COMMANDS_H

#include "lwip/sockets.h"
#include <stdint.h>

typedef struct {
    int sockfd;
    struct sockaddr_in addr;

    uint32_t exp_seq;
    uint32_t pack_recv;
    uint32_t pack_lost;

    uint16_t send_seq;

    struct in_addr allowed_ip4;

    struct {
        uint32_t pack_recv;
        uint32_t pack_lost;

        uint16_t exp_seq;
        uint16_t send_seq;

        uint32_t last_ts;
    } ping;
} commands_server_t;

int push_command(uint8_t command_type);

void commands_server_mgr_task(void *arg);

void clear_commands_sock_before_restart(void);

#endif /* COMMANDS_H */
