#include "command_handler.h"

#include <pthread.h>
#include <unistd.h>
#include <syslog.h>
#include <stdlib.h>
#include <string.h>

#include "discovery.h"
#include "rtp_client.h"

#include "daemon_protocol.h"

void *process_command_task(void *arg) {
    process_command_arg_t *pc_arg = (process_command_arg_t*) arg;
    daemon_rsp_t resp = {
        .type = pc_arg->cmd.type,
    };

    switch (pc_arg->cmd.type) {
        case DAEMON_CMD_STATUS:
            resp.status.connected = pc_arg->conn_data->is_running;
            strncpy(resp.status.ipv4, pc_arg->conn_data->ipv4, 15);
            break;
        case DAEMON_CMD_DISCOVERY:
            if (discover_task(pc_arg->disc_data, pc_arg->cmd.discovery.duration) != 0) {
                syslog(LOG_ERR, "Failed to discover headphones");
            }
        case DAEMON_CMD_DISCOVERY_DATA:
            resp.discovery.n_found = pc_arg->disc_data->count;
            memcpy(resp.discovery.found, pc_arg->disc_data->data,
                   pc_arg->disc_data->count * sizeof(device_info_t));
            break;
        case DAEMON_CMD_CONNECT:
            if (rtp_connection_start(pc_arg->conn_data, pc_arg->cmd.connect.ip4) != 0) {
                syslog(LOG_ERR, "Failed to start connection");
                resp.connect.success = false;
            } else {
                syslog(LOG_INFO, "Successfully connected to device with ip=%s", pc_arg->cmd.connect.ip4);
                resp.connect.success = true;
            }
            break;
        case DAEMON_CMD_DISCONNECT:
            rtp_connection_stop(pc_arg->conn_data);
            syslog(LOG_INFO, "Disconnecting from device");
            break;
        case DAEMON_CMD_UNKNOWN:
            syslog(LOG_WARNING, "Got DAEMON_CMD_UNKNOWN");
            break;
    }

    if (write(pc_arg->client_fd, &resp, sizeof(resp)) != sizeof(resp)) {
        syslog(LOG_ERR, "Failed to write daemon command response");
    }

    close(pc_arg->client_fd);
    free(arg);

    return NULL;
}
