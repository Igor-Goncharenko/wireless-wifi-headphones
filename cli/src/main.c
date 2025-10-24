#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <signal.h>

#include "info_msg.h"

#define MAX_COMMAND_LEN 256

static volatile sig_atomic_t keep_running = 1;

void signal_handler(int sig) {
    switch (sig) {
        case SIGTERM:
        case SIGINT:
            printf("\n%s", EXIT_MSG);
            keep_running = 0;
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

    while (keep_running) {
        printf("> ");
        if (fgets(cmd_buf, sizeof(cmd_buf), stdin) != NULL) {
            size_t len = strlen(cmd_buf);
            if (len > 0 && cmd_buf[len - 1] == '\n') {
                cmd_buf[len - 1] = '\0';
            }
            printf("cmd='%s'\n", cmd_buf);
        }
    }

    return EXIT_SUCCESS;
}
