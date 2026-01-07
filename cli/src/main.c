#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "command.h"
#include "protocols/daemon.h"
#include "info_msg.h"

#define SOCKET_PATH "/tmp/wifi_headphones_daemon.sock"

static volatile sig_atomic_t s_keep_running = 1;

static void signal_handler(int sig) {
    switch (sig) {
        case SIGTERM:
        case SIGINT:
            s_keep_running = 0;
            break;
        default:
            break;
    }
}

void init_signal_handler(void) {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
}

void print_daemon_resp(const daemon_rsp_t *resp) {
    switch (resp->type) {
        case DAEMON_CMD_STATUS:
            if (resp->status.connected)
                printf("Connected to %s\n", resp->status.ipv4);
            else
                printf("Not connected\n");
            break;
        case DAEMON_CMD_DISCOVERY:
        case DAEMON_CMD_DISCOVERY_DATA:
            printf("Total %d devices found\n", resp->discovery.n_found);
            for (int i = 0; i < resp->discovery.n_found; i++) {
                printf(" %d) name=\"%s\"; mac=\"%s\"; ipv4=\"%s\"; "
                       "sample_rate=%d; sample_size=%d; channels=%d\n",
                       i + 1,
                       resp->discovery.found[i].name,
                       resp->discovery.found[i].mac,
                       resp->discovery.found[i].ipv4,
                       resp->discovery.found[i].audio.sample_rate,
                       resp->discovery.found[i].audio.bit_width * 8,
                       resp->discovery.found[i].audio.channels);
            }
            break;
        case DAEMON_CMD_CONNECT:
            if (resp->connect.success)
                printf("Successfully connected\n");
            else
                printf("Failed to connect\n");
            break;
        case DAEMON_CMD_DISCONNECT:
        case DAEMON_CMD_UNKNOWN:
            break;
    }
}

int send_command_to_daemon(const daemon_cmd_t *cmd) {
    int sock = 0;
    struct sockaddr_un addr;

    if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("connect");
        close(sock);
        return -1;
    }

    if (write(sock, cmd, sizeof(daemon_cmd_t)) != sizeof(daemon_cmd_t)) {
        perror("write daemon_cmd_t");
        close(sock);
        return -1;
    }

    shutdown(sock, SHUT_WR);

    daemon_rsp_t resp;
    if (read(sock, &resp, sizeof(resp)) != sizeof(resp)) {
        perror("Failed to read daemon command response");
        close(sock);
        return -1;
    }
    print_daemon_resp(&resp);

    close(sock);
    return 0;
}

int process_cli_command(const char *cmd_str) {
    if (strcmp(cmd_str, "exit") == 0) {
        return 1;
    } else if (strcmp(cmd_str, "help") == 0 || strcmp(cmd_str, "?") == 0) {
        fputs(HELP_MSG, stdout);
        return 0;
    } else {
        daemon_cmd_t cmd;
        // printf("cmd='%s'; resp='%s'\n", cmd, command);
        if (process_command(cmd_str, &cmd) == 0) {
            send_command_to_daemon(&cmd);
        }
        return 0;
    }
}

int main(void) {
    init_signal_handler();

    char *cmd_buf = NULL;
    size_t cmd_buf_size = 0;

    fputs(HELLO_MSG, stdout);

    while (s_keep_running) {
        fputs(">>> ", stdout);

        if (getline(&cmd_buf, &cmd_buf_size, stdin) != -1) {
            size_t len = strlen(cmd_buf);
            if (len > 0 && cmd_buf[len - 1] == '\n') {
                cmd_buf[len - 1] = '\0';
            } else {
                continue;
            }

            if (process_cli_command(cmd_buf))
                break;
        }
    }

    if (cmd_buf != NULL)
        free(cmd_buf);

    fputs(EXIT_MSG, stdout);

    return EXIT_SUCCESS;
}
