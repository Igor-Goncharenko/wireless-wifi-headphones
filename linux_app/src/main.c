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

#include "audio.h"
#include "config.h"
#include "command_handler.h"
#include "discovery.h"
#include "ringbuf.h"
#include "rtp_client.h"

static volatile sig_atomic_t keep_running = 1;

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

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    return 0;
}

void daemon_cleanup(void) {
    unlink(DAEMON_SOCKET_PATH);
    
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
    strncpy(addr.sun_path, DAEMON_SOCKET_PATH, sizeof(addr.sun_path) - 1);

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
    unlink(DAEMON_SOCKET_PATH);

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

int main(void) {
    int ret;
    discovery_data_t discovery_data = { 0 };
    rtp_connection_data_t connection_data = { 0 };
    pulse_audio_t pulse = { 0 };

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

    if (audio_init(&pulse) != 0) {
        syslog(LOG_ERR, "Failed to init audio");
        audio_destroy(&pulse);
        return -1;
    }

    const int sockfd = create_socket();
    if (sockfd < 0) {
        syslog(LOG_ERR, "Failed to create socket");
        daemon_cleanup();
        closelog();
        return EXIT_FAILURE;
    }

    if (discovery_data_init(&discovery_data) != 0) {
        syslog(LOG_ERR, "Failed to init discovery data");
        daemon_cleanup();
        audio_destroy(&pulse);
        close_socket(sockfd);
        closelog();
        return EXIT_FAILURE;
    }

    if (rtp_connection_data_init(&connection_data, &pulse.audio_buf) != 0) {
        syslog(LOG_ERR, "Failed to init connection  data");
        daemon_cleanup();
        audio_destroy(&pulse);
        close_socket(sockfd);
        discovery_data_destroy(&discovery_data);
        closelog();
        return EXIT_FAILURE;
    }

    while (keep_running) {
        process_command_arg_t *arg = malloc(sizeof(process_command_arg_t));
        if (!arg) {
            syslog(LOG_ERR, "Failed to allocated memory for process_command_arg");
            continue;
        }
        arg->disc_data = &discovery_data;
        arg->conn_data = &connection_data;

        arg->client_fd = accept(sockfd, NULL, NULL);
        if (arg->client_fd < 0) {
            if (errno != EINTR) {
                syslog(LOG_ERR, "Accept failed: %s", strerror(errno));
            }
            free(arg);
            continue;
        }

        ssize_t bytes = read(arg->client_fd, &arg->cmd, sizeof(arg->cmd));

        if (bytes > 0) {
            pthread_t tid;

            if ((ret = pthread_create(&tid, NULL, process_command_task, arg)) == 0) {
                pthread_detach(tid);
            } else {
                syslog(LOG_ERR, "Failed to create thread: %s", strerror(ret));
                close(arg->client_fd);
                free(arg);
            }
        } else {
            close(arg->client_fd);
            free(arg);
        }
    }

    syslog(LOG_INFO, "Shutting down gracefully...");
    daemon_cleanup();
    audio_destroy(&pulse);
    close_socket(sockfd);
    discovery_data_destroy(&discovery_data);
    rtp_connection_data_destroy(&connection_data);
    closelog();

    return EXIT_SUCCESS;
}
