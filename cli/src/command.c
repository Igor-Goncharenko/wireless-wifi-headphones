#include "command.h"

#include <stdio.h>
#include <string.h>

#include <cJSON.h>

#include "wh_daemon_commands.h"

static int _status_cb(const char *cmd_raw, command_t *cmd);
static int _discovery_cb(const char *cmd_raw, command_t *cmd);
static int _discovery_data_cb(const char *cmd_raw, command_t *cmd);
static int _connect_cb(const char *cmd_raw, command_t *cmd);
static int _disconnect_cb(const char *cmd_raw, command_t *cmd);

static const cli_command_t COMMANDS[COMMAND_TYPE_LAST + 1] = {
    [COMMAND_STATUS] = {
        .cmd = "status",
        .cb = _status_cb,
    },
    [COMMAND_DISCOVERY] = {
        .cmd = "discovery",
        .cb = _discovery_cb,
    },
    [COMMAND_DISCOVERY_DATA] = {
        .cmd = "discovery_data",
        .cb = _discovery_data_cb,
    },
    [COMMAND_CONNECT] = {
        .cmd = "connect",
        .cb = _connect_cb,
    },
    [COMMAND_DISCONNECT] = {
        .cmd = "disconnect",
        .cb = _disconnect_cb,
    },
};

char *process_command(const char *cmd_str) {
    for (int i = 0; i < COMMAND_TYPE_LAST + 1; i++) {
        size_t len = strlen(COMMANDS[i].cmd);
        char after_cmd = cmd_str[len];
        if (strncmp(COMMANDS[i].cmd, cmd_str, len) == 0 && (after_cmd == ' ' || after_cmd == '\0')) {
            command_t cmd;
            COMMANDS[i].cb(cmd_str, &cmd);
            return command_to_string(&cmd);
        }
    }
    return NULL;
}

static int _status_cb(const char *cmd_raw, command_t *cmd) {
    cmd->type = COMMAND_STATUS;
    return 0;
}

static int _discovery_cb(const char *cmd_raw, command_t *cmd) {
    int duration, ret;

    ret = sscanf(cmd_raw, "discovery %d", &duration);

    cmd->type = COMMAND_DISCOVERY;
    if (ret == 1)
        cmd->discovery.duration = duration;
    else
        cmd->discovery.duration = 5;
    return 0;
}

static int _discovery_data_cb(const char *cmd_raw, command_t *cmd) {
    cmd->type = COMMAND_DISCOVERY_DATA;
    return 0;
}

static int _connect_cb(const char *cmd_raw, command_t *cmd) {
    int ret;

    cmd->type = COMMAND_CONNECT;

    ret = sscanf(cmd_raw, "connect %15s", cmd->connect.ip4);
    return (ret == 1) ? 0 : -1;
}

static int _disconnect_cb(const char *cmd_raw, command_t *cmd) {
    int ret;

    cmd->type = COMMAND_DISCONNECT;

    ret = sscanf(cmd_raw, "disconnect %15s", cmd->connect.ip4);
    return (ret == 1) ? 0 : -1;
}
