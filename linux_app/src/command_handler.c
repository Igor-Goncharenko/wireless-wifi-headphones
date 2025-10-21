#include "command_handler.h"

#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <syslog.h>
#include <stdlib.h>

#include <cJSON.h>

#include "discovery.h"
#include "rtp_client.h"

static void parse_discovery_command(cJSON *data, discovery_command_data_t *out) {
    if (data == NULL) {
        out->duration = DEFAULT_DISCOVERY_DURATION;
        return;
    }

    cJSON *duration = cJSON_GetObjectItemCaseSensitive(data, "duration");
    if (duration != NULL && cJSON_IsNumber(duration) && duration->valueint > 0 && 
        duration->valueint <= MAX_DISCOVERY_DURATION) {
        out->duration = duration->valueint;
    } else {
        syslog(LOG_WARNING, "Got incorrect data in discovery command");
        out->duration = DEFAULT_DISCOVERY_DURATION;
    }
}

static int parse_connect_command(cJSON *data, connect_command_data_t *out) {
    if (data == NULL) {
        syslog(LOG_WARNING, "Got NULL data in connect command");
        return -1;
    }

    cJSON *ip4 = cJSON_GetObjectItemCaseSensitive(data, "ip4");
    if (ip4 != NULL && cJSON_IsString(ip4)) {
        const size_t ip4_len = strlen(ip4->valuestring);
        const size_t cpy_len = (sizeof(out->ip4) - 1) > ip4_len ? ip4_len : sizeof(out->ip4) - 1;
        strncpy(out->ip4, ip4->valuestring, cpy_len);
        out->ip4[cpy_len] = '\0';
    } else {
        syslog(LOG_WARNING, "Got incorrect data in connect command");
        return -1;
    }

    return 0;
}

static int parse_command(const char *command_json, command_t *cmd) {
    cJSON *root = cJSON_Parse(command_json);
    if (root == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            syslog(LOG_ERR, "JSON parse error: %s", error_ptr);
        }
        return -1;
    }

    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (cJSON_IsNumber(type) && type->valueint >= 0 && type->valueint <= COMMAND_TYPE_LAST) {
        cmd->type = (command_type_e)type->valueint;
    } else {
        cmd->type = COMMAND_UNKNOWN;
        cJSON_Delete(root);
        return -1;
    }

    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    int ret = 0;

    switch (cmd->type) {
        case COMMAND_DISCOVERY:
            parse_discovery_command(data, &cmd->discovery);
            break;
        case COMMAND_DISCONNECT:
        case COMMAND_CONNECT:
            ret = parse_connect_command(data, &cmd->connect);
            break;
        default:
            break;
    }
    
    if (ret != 0) {
        cJSON_Delete(root);
        syslog(LOG_ERR, "Failed to parse command data");
        return -1;
    }

    cJSON_Delete(root);
    return 0;
}

static void send_discovery_data(const int client_fd, discovery_data_t *data) {
    char buffer[256];
    int size;

    pthread_mutex_lock(&data->mutex);
    
    if (data->count > 0) {
        size = sprintf(buffer, "devices=%d:\n", data->count);
        write(client_fd, buffer, size);

        for (int i = 0; i < data->count; i++) {
            size = sprintf(buffer, " %d) type=%s; model=%s; id=%s; ip4=%s\n", 
                           i + 1, data->data[i].type, data->data[i].model, data->data[i].id, 
                           data->data[i].ip_v4);
            write(client_fd, buffer, size);
        }
    } else {
        const char response[] = "No devices found\n";
        write(client_fd, response, sizeof(response) - 1);
    }

    pthread_mutex_unlock(&data->mutex);
}

static int discover_and_send_data(const int client_fd, discovery_data_t *data, const int duration) {
    if (discover_task(data, duration) != 0) {
        syslog(LOG_ERR, "Failed to discover headphones");
        return -1;
    }

    send_discovery_data(client_fd, data);

    return 0;
}

static int connect_device(rtp_connection_data_t *conn_data, const char *ip4) {
    if (rtp_connection_start(conn_data, ip4) != 0) {
        syslog(LOG_ERR, "Failed to start connection");
        return -1;
    }
    syslog(LOG_INFO, "Successfully connected to device with ip=%s", ip4);
    return 0;
}

static int disconnect_device(rtp_connection_data_t *conn_data) {
    rtp_connection_stop(conn_data);
    syslog(LOG_INFO, "Disconnecting from device");
    return 0;
}

void *process_command_task(void *arg) {
    process_command_arg_t *pc_arg = (process_command_arg_t*) arg;
    command_t command;

    if (parse_command(pc_arg->command, &command) != 0) {
        syslog(LOG_ERR, "Failed to parse command");
        close(pc_arg->client_fd);
        free(arg);
        return NULL;
    }

    const char STATUS_RESP[] = "Daemon is working\n";

    switch (command.type) {
        case COMMAND_STATUS:
            write(pc_arg->client_fd, STATUS_RESP, sizeof(STATUS_RESP) - 1);
            break;
        case COMMAND_DISCOVERY:
            discover_and_send_data(pc_arg->client_fd, pc_arg->disc_data, command.discovery.duration);
            break;
        case COMMAND_DISCOVERY_DATA:
            send_discovery_data(pc_arg->client_fd, pc_arg->disc_data);
            break;
        case COMMAND_CONNECT:
            connect_device(pc_arg->conn_data, command.connect.ip4);
            break;
        case COMMAND_DISCONNECT:
            disconnect_device(pc_arg->conn_data);
            break;
        case COMMAND_UNKNOWN:
            syslog(LOG_WARNING, "Got COMMAND_UNKNOWN");
            break;
    }

    close(pc_arg->client_fd);
    free(arg);

    return NULL;
}
