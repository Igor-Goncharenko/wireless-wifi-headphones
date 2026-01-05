#include "command.h"

#include <stdio.h>
#include <string.h>

#include "daemon_protocol.h"

static int _status_cb(const char *cmd_raw, daemon_cmd_t *cmd) {
    cmd->type = DAEMON_CMD_STATUS;
    return 0;
}

static int _discovery_cb(const char *cmd_raw, daemon_cmd_t *cmd) {
    int duration, ret;

    ret = sscanf(cmd_raw, "discovery %d", &duration);

    cmd->type = DAEMON_CMD_DISCOVERY;
    if (ret == 1)
        cmd->discovery.duration = duration;
    else
        cmd->discovery.duration = 5;
    return 0;
}

static int _discovery_data_cb(const char *cmd_raw, daemon_cmd_t *cmd) {
    cmd->type = DAEMON_CMD_DISCOVERY_DATA;
    return 0;
}

static int _connect_cb(const char *cmd_raw, daemon_cmd_t *cmd) {
    int ret;

    cmd->type = DAEMON_CMD_CONNECT;

    ret = sscanf(cmd_raw, "connect %15s", cmd->connect.ip4);
    return (ret == 1) ? 0 : -1;
}

static int _disconnect_cb(const char *cmd_raw, daemon_cmd_t *cmd) {
    int ret;

    cmd->type = DAEMON_CMD_DISCONNECT;

    ret = sscanf(cmd_raw, "disconnect %15s", cmd->connect.ip4);
    return (ret == 1) ? 0 : -1;
}

static const cli_command_t COMMANDS[DAEMON_CMD_TYPE_LAST + 1] = {
    [DAEMON_CMD_STATUS] = {
        .cmd = "status",
        .cb = _status_cb,
    },
    [DAEMON_CMD_DISCOVERY] = {
        .cmd = "discovery",
        .cb = _discovery_cb,
    },
    [DAEMON_CMD_DISCOVERY_DATA] = {
        .cmd = "discovery_data",
        .cb = _discovery_data_cb,
    },
    [DAEMON_CMD_CONNECT] = {
        .cmd = "connect",
        .cb = _connect_cb,
    },
    [DAEMON_CMD_DISCONNECT] = {
        .cmd = "disconnect",
        .cb = _disconnect_cb,
    },
};

int process_command(const char *cmd_str, daemon_cmd_t *dest) {
    for (int i = 0; i < DAEMON_CMD_TYPE_LAST + 1; i++) {
        size_t len = strlen(COMMANDS[i].cmd);
        char after_cmd = cmd_str[len];
        if (strncmp(COMMANDS[i].cmd, cmd_str, len) == 0 && (after_cmd == ' ' || after_cmd == '\0')) {
            if (COMMANDS[i].cb(cmd_str, dest) != 0)
                return -1;
            return 0;
        }
    }

    return -1;
}
