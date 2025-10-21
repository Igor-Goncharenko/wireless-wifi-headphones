#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include "discovery.h"

#define MAX_COMMAND_LEN 256

typedef struct {
    int client_fd;
    char command[MAX_COMMAND_LEN];
    discovery_data_t *data_ptr;
} process_command_arg_t;

void *process_command_task(void *arg);

#endif /* COMMAND_HANDLER_H */
