#include "wh_daemon_commands.h"

#include <string.h>

#include <cJSON.h>

#define MAX_DISCOVERY_DURATION 10

static void _daemon_command_parse_discovery(cJSON *data, daemon_cmd_discovery_t *out) {
    if (data == NULL) {
        out->duration = DEFAULT_DISCOVERY_DURATION;
        return;
    }

    cJSON *duration = cJSON_GetObjectItemCaseSensitive(data, "duration");
    if (duration != NULL && cJSON_IsNumber(duration) && duration->valueint > 0 && 
        duration->valueint <= MAX_DISCOVERY_DURATION) {
        out->duration = duration->valueint;
    } else {
        // syslog(LOG_WARNING, "Got incorrect data in discovery command");
        out->duration = DEFAULT_DISCOVERY_DURATION;
    }
}

static int _daemon_cmd_parse_connect(cJSON *data, daemon_cmd_connect_t *out) {
    if (data == NULL) {
        // syslog(LOG_WARNING, "Got NULL data in connect command");
        return -1;
    }

    cJSON *ip4 = cJSON_GetObjectItemCaseSensitive(data, "ip4");
    if (ip4 != NULL && cJSON_IsString(ip4)) {
        const size_t ip4_len = strlen(ip4->valuestring);
        const size_t cpy_len = (sizeof(out->ip4) - 1) > ip4_len ? ip4_len : sizeof(out->ip4) - 1;
        strncpy(out->ip4, ip4->valuestring, cpy_len);
        out->ip4[cpy_len] = '\0';
    } else {
        // syslog(LOG_WARNING, "Got incorrect data in connect command");
        return -1;
    }

    return 0;
}

int daemon_cmd_parse(const char *command_json, daemon_cmd_t *cmd) {
    cJSON *root = cJSON_Parse(command_json);
    if (root == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            // syslog(LOG_ERR, "JSON parse error: %s", error_ptr);
        }
        return -1;
    }

    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (cJSON_IsNumber(type) && type->valueint >= 0 && type->valueint <= DAEMON_CMD_TYPE_LAST) {
        cmd->type = (daemon_cmd_type_e)type->valueint;
    } else {
        cmd->type = DAEMON_CMD_UNKNOWN;
        cJSON_Delete(root);
        return -1;
    }

    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    int ret = 0;

    switch (cmd->type) {
        case DAEMON_CMD_DISCOVERY:
            _daemon_command_parse_discovery(data, &cmd->discovery);
            break;
        case DAEMON_CMD_DISCONNECT:
        case DAEMON_CMD_CONNECT:
            ret = _daemon_cmd_parse_connect(data, &cmd->connect);
            break;
        default:
            break;
    }

    if (ret != 0) {
        cJSON_Delete(root);
        // syslog(LOG_ERR, "Failed to parse command data");
        return -1;
    }

    cJSON_Delete(root);
    return 0;
}

char *daemon_cmd_to_string(const daemon_cmd_t *cmd) {
    if (cmd->type == DAEMON_CMD_UNKNOWN) return NULL;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "type", cmd->type);

    if (cmd->type == DAEMON_CMD_DISCOVERY || cmd->type == DAEMON_CMD_CONNECT ||
        cmd->type == DAEMON_CMD_DISCONNECT) {
        cJSON *data = cJSON_CreateObject();

        switch (cmd->type) {
            case DAEMON_CMD_DISCOVERY:
                cJSON_AddNumberToObject(data, "duration", cmd->discovery.duration);
                break;
            case DAEMON_CMD_CONNECT:
            case DAEMON_CMD_DISCONNECT:
                cJSON_AddStringToObject(data, "ip", cmd->connect.ip4);
                break;
            default:
                break;
        }

        cJSON_AddItemToObject(root, "data", data);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    return json_str;
}
