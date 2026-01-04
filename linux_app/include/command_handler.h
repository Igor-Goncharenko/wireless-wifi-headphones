#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include "discovery.h"
#include "rtp_client.h"

#define MAX_COMMAND_LEN 256

typedef struct {
    int client_fd;
    char command[MAX_COMMAND_LEN];
    discovery_data_t *disc_data;
    rtp_connection_data_t *conn_data;
} process_command_arg_t;

void *process_command_task(void *arg);

#endif /* COMMAND_HANDLER_H */
