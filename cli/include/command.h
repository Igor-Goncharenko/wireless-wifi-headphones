#ifndef COMMAND_H
#define COMMAND_H

#define CLI_CMD_MAX_LEN 32
#define CLI_CMD_MAX_HELP_LEN 1024

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

typedef int (*cli_command_cb)(const char *cmd_raw, command_t *cmd);

typedef struct {
    char cmd[CLI_CMD_MAX_LEN];
    char help[CLI_CMD_MAX_HELP_LEN];
    cli_command_cb cb;
} cli_command_t;

char *process_command(const char *cmd_str);

#endif /* COMMAND_H */
