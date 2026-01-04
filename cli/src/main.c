#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "command.h"
#include "info_msg.h"

#define MAX_COMMAND_LEN 256
#define SOCKET_PATH "/tmp/wifi_headphones_daemon.sock"

static volatile sig_atomic_t s_keep_running = 1;

void signal_handler(int sig) {
    switch (sig) {
        case SIGTERM:
        case SIGINT:
            printf("\n%s", EXIT_MSG);
            s_keep_running = 0;
            break;
        default:
            break;
    }
}

int send_command_to_daemon(const char *command) {
    printf("command=\"%s\"\n", command);
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

    if (dprintf(sock, "%s\n", command) < 0) {
        perror("dprintf");
        close(sock);
        return -1;
    }

    shutdown(sock, SHUT_WR);

    char buffer[1024];
    ssize_t bytes_received = read(sock, buffer, sizeof(buffer) - 1);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        printf("Response from daemon: \"%s\"\n", buffer);
    }

    close(sock);
    return 0;
}

int main(void) {
    char cmd_buf[MAX_COMMAND_LEN];

    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    printf("%s", HELLO_MSG);

    while (s_keep_running) {
        printf("> ");
        if (fgets(cmd_buf, sizeof(cmd_buf), stdin) != NULL) {
            size_t len = strlen(cmd_buf);
            if (len > 0 && cmd_buf[len - 1] == '\n') {
                cmd_buf[len - 1] = '\0';
            }
            char *command = process_command(cmd_buf);
            printf("cmd='%s'; resp='%s'\n", cmd_buf, command);
            if (command) {
                send_command_to_daemon(command);
                free(command);
            }
        }
    }

    return EXIT_SUCCESS;
}
