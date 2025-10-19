#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/syslog.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>

#include "ringbuf.h"
#include "rtp_client.h"

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

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    logfile = fopen("/tmp/" CONFIG_DAEMON_NAME ".log", "a");
    if (!logfile) {
        syslog(LOG_WARNING, "Failed to open log file: errno=%d, strerror=\"%s\"",
                errno, strerror(errno));
    }

    return 0;
}

void daemon_cleanup(void) {
    if (logfile != NULL) {
        fclose(logfile);
        logfile = NULL;
        syslog(LOG_DEBUG, "log file closed");
    }

    unlink("/var/run/" CONFIG_DAEMON_NAME ".pid");
    
    syslog(LOG_INFO, "Daemon cleanup complete");
    closelog();
}

void daemon_workloop(void) {
    int iteration = 0;
    
    while(keep_running) {
        syslog(LOG_DEBUG, "Iteration %d", iteration++);
        
        if (logfile) {
            fprintf(logfile, "Iteration %d\n", iteration);
            fflush(logfile);
        }
        
        int slept = 0;
        while(keep_running && slept < 10) {
            sleep(1);
            slept++;
        }
    }
}

void signal_handler(int sig) {
    switch (sig) {
        case SIGTERM:
        case SIGINT:
            syslog(LOG_INFO, "Got SIGTERM, stop");
            closelog();
            keep_running = 0;
            break;
        default:
            break;
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

    atexit(daemon_cleanup);

    daemon_workloop();

    daemon_cleanup();

    return EXIT_SUCCESS;
}
