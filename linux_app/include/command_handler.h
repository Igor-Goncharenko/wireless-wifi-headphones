#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include "discovery.h"

#define MAX_COMMAND_LEN 256
#define DEFAULT_DISCOVERY_DURATION 5

typedef struct {
    int client_fd;
    char command[MAX_COMMAND_LEN];
    discovery_data_t *data_ptr;
} process_command_arg_t;

#define COMMAND_TYPE_LAST COMMAND_DISCOVERY_DATA
typedef enum {
    COMMAND_UNKNOWN = -1,
    COMMAND_STATUS = 0,
    COMMAND_DISCOVERY,
    COMMAND_DISCOVERY_DATA,
} command_type_e;

typedef struct {
    int duration;
} discovery_command_data_t;

typedef struct {
    command_type_e type;
    union {
        discovery_command_data_t discovery;
    };
} command_t;

void *process_command_task(void *arg);

#endif /* COMMAND_HANDLER_H */
