#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include "discovery.h"
#include "rtp_client.h"

#define MAX_COMMAND_LEN 256
#define DEFAULT_DISCOVERY_DURATION 5

typedef struct {
    int client_fd;
    char command[MAX_COMMAND_LEN];
    discovery_data_t *disc_data;
    rtp_connection_data_t *conn_data;
} process_command_arg_t;

#define COMMAND_TYPE_LAST COMMAND_DISCONNECT
typedef enum {
    COMMAND_UNKNOWN = -1,
    COMMAND_STATUS = 0,
    COMMAND_DISCOVERY,
    COMMAND_DISCOVERY_DATA,
    COMMAND_CONNECT,
    COMMAND_DISCONNECT,
} command_type_e;

typedef struct {
    int duration;
} discovery_command_data_t;

typedef struct {
    char ip4[16];
} connect_command_data_t;

typedef struct {
    command_type_e type;
    union {
        discovery_command_data_t discovery;
        connect_command_data_t connect;
    };
} command_t;

void *process_command_task(void *arg);

#endif /* COMMAND_HANDLER_H */
