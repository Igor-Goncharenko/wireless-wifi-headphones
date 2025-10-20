#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/syslog.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "ringbuf.h"
#include "rtp_client.h"

#define SOCKET_PATH "/tmp/" CONFIG_DAEMON_NAME ".sock"
#define LOGFILE_PATH "/tmp/" CONFIG_DAEMON_NAME ".log"

static volatile sig_atomic_t keep_running = 1;
static FILE *logfile = NULL;

struct rtp_send_thread {
    rtp_session_t *session;
    ringbuf_t *buf;
};

int daemon_init(void) {
    pid_t pid;

    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "Fork error: errno=%d, strerror=\"%s\"\n", errno, strerror(errno));
        return -1;
    }
    if (pid > 0) {
        return 1;
    }

    if (setsid() < 0) {
        fprintf(stderr, "setsid() error: errno=%d, strerror=\"%s\"\n", errno, strerror(errno));
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "Fork error: errno=%d, strerror=\"%s\"\n", errno, strerror(errno));
        return -1;
    }
    if (pid > 0) {
        return 1;
    }

    chdir("/");
    umask(0);

    logfile = fopen(LOGFILE_PATH, "a");
    if (!logfile) {
        fprintf(stderr, "Failed to open log file: errno=%d, strerror=\"%s\"",
                errno, strerror(errno));
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    return 0;
}

void daemon_cleanup(void) {
    if (logfile != NULL) {
        fclose(logfile);
        logfile = NULL;
        syslog(LOG_DEBUG, "log file closed");
    }

    unlink(SOCKET_PATH);
    
    syslog(LOG_INFO, "Daemon cleanup complete");
}

int create_socket(void) {
    int sockfd;
    struct sockaddr_un addr;

    if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        syslog(LOG_ERR, "Failed to create unix socket");
        return -1;
    }

    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        syslog(LOG_ERR, "Failed to bind unix socket");
        close(sockfd);
        return -1;
    }
    if (listen(sockfd, 5) < 0) {
        syslog(LOG_ERR, "Failed to listen unix socket");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

void close_socket(int sockfd) {
    if (sockfd > 0) {
        close(sockfd);
    }
    unlink(SOCKET_PATH);

}

void signal_handler(int sig) {
    switch (sig) {
        case SIGTERM:
        case SIGINT:
            syslog(LOG_INFO, "Got SIGTERM, stop");
            keep_running = 0;
            break;
        default:
            break;
    }
}

void process_command(const char *cmd, const int client_fd) {
    if (strcmp(cmd, "STATUS") == 0) {
        const char *response = "Daemon is working\n";
        if (write(client_fd, response, strlen(response)) < 0) {
            syslog(LOG_ERR, "Failed write command response");
        } else {
            syslog(LOG_INFO, "Got status command");
        }
    } else {
        syslog(LOG_WARNING, "Unknown command %s", cmd);
    }
}

void *send_data_with_rtp(void *arg) {
    struct rtp_send_thread *rst = (struct rtp_send_thread*)arg;

    while (keep_running) {
        uint8_t data[FRAMES_PER_PACKET];
        size_t read = ringbuf_read_block(rst->buf, data, FRAMES_PER_PACKET);
        rtp_send_packet(rst->session, data, read, 0);
        usleep(1000);
    }

    return NULL;
}

int main(void) {
    int ret;

    ret = daemon_init();
    if (ret == 1) {
        return EXIT_SUCCESS;
    } else if (ret == -1) {
        fprintf(stderr, "Failed to init daemon\n");
        return EXIT_FAILURE;
    }

    openlog(CONFIG_DAEMON_NAME, LOG_PID, LOG_DAEMON);
    syslog(LOG_INFO, "daemon " CONFIG_DAEMON_NAME " started");

    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    const int sockfd = create_socket();

    while (keep_running) {
        int client_fd = accept(sockfd, NULL, NULL);
        char buffer[256];
        ssize_t bytes = read(client_fd, buffer, sizeof(buffer) - 1);
        if (bytes > 0) {
            buffer[bytes - 1] = '\0';
            process_command(buffer, client_fd);
        }
        close(client_fd);
    }

    daemon_cleanup();
    close_socket(sockfd);
    closelog();

    return EXIT_SUCCESS;
}
