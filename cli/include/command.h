#ifndef COMMAND_H
#define COMMAND_H

#define CLI_CMD_MAX_LEN 32
#define CLI_CMD_MAX_HELP_LEN 1024

#include "wh_daemon_commands.h"

typedef int (*cli_command_cb)(const char *cmd_raw, daemon_cmd_t *cmd);

typedef struct {
    char cmd[CLI_CMD_MAX_LEN];
    char help[CLI_CMD_MAX_HELP_LEN];
    cli_command_cb cb;
} cli_command_t;

int process_command(const char *cmd_str, daemon_cmd_t *dest);

#endif /* COMMAND_H */
