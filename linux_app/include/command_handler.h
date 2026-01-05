#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include "discovery.h"
#include "rtp_client.h"
#include "daemon_protocol.h"

typedef struct {
    int client_fd;
    daemon_cmd_t cmd;
    discovery_data_t *disc_data;
    rtp_connection_data_t *conn_data;
} process_command_arg_t;

void *process_command_task(void *arg);

#endif /* COMMAND_HANDLER_H */
