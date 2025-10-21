#include "command_handler.h"

#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <stdlib.h>

#include "discovery.h"

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

static int discover_and_send_data(const int client_fd, discovery_data_t *data) {
    if (discover_task(data) != 0) {
        syslog(LOG_ERR, "Failed to discover headphones");
        return -1;
    }

    send_discovery_data(client_fd, data);

    return 0;
}

void *process_command_task(void *arg) {
    process_command_arg_t *pc_arg = (process_command_arg_t*) arg;

    if (strcmp(pc_arg->command, "STATUS") == 0) {
        const char *response = "Daemon is working\n";
        write(pc_arg->client_fd, response, strlen(response));
    } else if (strcmp(pc_arg->command, "DISCOVERY") == 0) {
        discover_and_send_data(pc_arg->client_fd, pc_arg->data_ptr);
    } else if (strcmp(pc_arg->command, "DISCOVERY_DATA") == 0) {
        send_discovery_data(pc_arg->client_fd, pc_arg->data_ptr);
    } else {
        syslog(LOG_WARNING, "Unknown command %s", pc_arg->command);
    }

    close(pc_arg->client_fd);
    free(arg);

    return NULL;
}
