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

#include "discovery.h"
#include "ringbuf.h"
#include "rtp_client.h"

#define SOCKET_PATH "/tmp/" CONFIG_DAEMON_NAME ".sock"
#define LOGFILE_PATH "/tmp/" CONFIG_DAEMON_NAME ".log"

#define MAX_COMMAND_LEN 256

static volatile sig_atomic_t keep_running = 1;
static FILE *logfile = NULL;

struct rtp_send_thread {
    rtp_session_t *session;
    ringbuf_t *buf;
};

struct process_command_arg {
    int client_fd;
    char command[MAX_COMMAND_LEN];
    discovery_data_t *data_ptr;
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

void send_discovery_data(const int client_fd, discovery_data_t *data) {
    char buffer[256];
    int size;

    pthread_mutex_lock(&data->mutex);
    
    if (data->count > 0) {
        size = sprintf(buffer, "devices=%d\n:", data->count);
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

int discover_and_send_data(const int client_fd, discovery_data_t *data) {
    if (discover_task(data) != 0) {
        syslog(LOG_ERR, "Failed to discover headphones");
        return -1;
    }

    send_discovery_data(client_fd, data);

    return 0;
}

void *process_command_task(void *arg) {
    struct process_command_arg *pc_arg = (struct process_command_arg*) arg;

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
    discovery_data_t discovery_data;

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
    if (sockfd < 0) {
        syslog(LOG_ERR, "Failed to create socket");
        daemon_cleanup();
        closelog();
        return EXIT_FAILURE;
    }

    if (discovery_data_init(&discovery_data) != 0) {
        syslog(LOG_ERR, "Failed to init discovery data");
        daemon_cleanup();
        close_socket(sockfd);
        closelog();
        return EXIT_FAILURE;
    }

    while (keep_running) {
        struct process_command_arg *arg = malloc(sizeof(struct process_command_arg));
        if (!arg) {
            syslog(LOG_ERR, "Failed to allocated memory for process_command_arg");
            continue;
        }
        arg->data_ptr = &discovery_data;

        arg->client_fd = accept(sockfd, NULL, NULL);
        if (arg->client_fd < 0) {
            if (errno != EINTR) {
                syslog(LOG_ERR, "Accept failed: %s", strerror(errno));
            }
            free(arg);
            continue;
        }

        ssize_t bytes = read(arg->client_fd, arg->command, MAX_COMMAND_LEN - 1);

        if (bytes > 0) {
            pthread_t tid;

            arg->command[bytes - 1] = '\0';
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
    close_socket(sockfd);
    discovery_data_destroy(&discovery_data);
    closelog();

    return EXIT_SUCCESS;
}
