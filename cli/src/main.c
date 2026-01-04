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
                free(command);
            }
        }
    }

    return EXIT_SUCCESS;
}
