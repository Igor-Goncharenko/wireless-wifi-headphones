#ifndef WH_DAEMON_COMMANDS_H
#define WH_DAEMON_COMMANDS_H

#define DEFAULT_DISCOVERY_DURATION 5

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

char *command_to_string(const command_t *cmd);

int parse_command(const char *command_json, command_t *cmd);

#endif /* WH_DAEMON_COMMANDS_H */
