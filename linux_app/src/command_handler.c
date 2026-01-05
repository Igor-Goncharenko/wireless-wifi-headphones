#include "command_handler.h"

#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <syslog.h>
#include <stdlib.h>

#include "discovery.h"
#include "rtp_client.h"

#include "wh_daemon_commands.h"

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

    const char STATUS_RESP[] = "Daemon is working\n";

    switch (pc_arg->cmd.type) {
        case DAEMON_CMD_STATUS:
            write(pc_arg->client_fd, STATUS_RESP, sizeof(STATUS_RESP) - 1);
            break;
        case DAEMON_CMD_DISCOVERY:
            discover_and_send_data(pc_arg->client_fd, pc_arg->disc_data, pc_arg->cmd.discovery.duration);
            break;
        case DAEMON_CMD_DISCOVERY_DATA:
            send_discovery_data(pc_arg->client_fd, pc_arg->disc_data);
            break;
        case DAEMON_CMD_CONNECT:
            connect_device(pc_arg->conn_data, pc_arg->cmd.connect.ip4);
            break;
        case DAEMON_CMD_DISCONNECT:
            disconnect_device(pc_arg->conn_data);
            break;
        case DAEMON_CMD_UNKNOWN:
            syslog(LOG_WARNING, "Got DAEMON_CMD_UNKNOWN");
            break;
    }

    close(pc_arg->client_fd);
    free(arg);

    return NULL;
}
