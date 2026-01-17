#include "headphones_commands.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>

#include "config.h"
#include "protocols/headphones.h"

#define RECV_ERR_DELAY_US 100000
#define HPCMD_QUEUE_POP_TIMEOUT_MS 10

#define HPCMD_SOCK_TIMEOUT_MS 1000

static int hpcmd_queue_init(hpcmd_queue_t *q) {
    memset(q, 0, sizeof(hpcmd_queue_t));
    q->front = 0;
    q->back = 0;
    q->len = 0;
    if (pthread_mutex_init(&q->mutex, NULL) != 0) {
        syslog(LOG_ERR, "HPCMD failed to init session queue mutex");
        return -1;
    }
    if (pthread_cond_init(&q->new_item_cond, NULL) != 0) {
        syslog(LOG_ERR, "HPCMD failed to init session queue mutex");
        pthread_mutex_destroy(&q->mutex);
        return -1;
    }
    return 0;
}

static void hpcmd_queue_destroy(hpcmd_queue_t *q) {
    pthread_mutex_lock(&q->mutex);
    pthread_mutex_unlock(&q->mutex);
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->new_item_cond);
}

static int hpcmd_queue_push(hpcmd_session_t *session, headphones_packet_t *command) {
    pthread_mutex_lock(&session->queue.mutex);

    if (session->queue.len == HPCMD_QUEUE_SIZE) {
        syslog(LOG_WARNING, "HPCMD not enough space in queue, drop command");
        pthread_mutex_unlock(&session->queue.mutex);
        return -1;
    }
    memcpy(&session->queue.data[session->queue.back], command, sizeof(headphones_packet_t));
    session->queue.back = (session->queue.back + 1) % HPCMD_QUEUE_SIZE;
    session->queue.len++;

    if (session->queue.len == 1) {
        pthread_cond_signal(&session->queue.new_item_cond);
    }
    pthread_mutex_unlock(&session->queue.mutex);
    return 0;
}

static bool hpcmd_queue_pop(hpcmd_session_t *session, headphones_packet_t *dest) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += HPCMD_QUEUE_POP_TIMEOUT_MS / 1000;
    ts.tv_nsec += (HPCMD_QUEUE_POP_TIMEOUT_MS % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec += ts.tv_nsec / 1000000000;
        ts.tv_nsec %= 1000000000;
    }

    pthread_mutex_lock(&session->queue.mutex);

    while (session->queue.len == 0) {
        int ret = pthread_cond_timedwait(&session->queue.new_item_cond,
                                         &session->queue.mutex, &ts);
        if (ret != 0) {
            if (ret != ETIMEDOUT) {
                syslog(LOG_ERR, "pthread_cond_timedwait error: %d", ret);
            }
            pthread_mutex_unlock(&session->queue.mutex);
            return false;
        }
    }

    memcpy(dest, &session->queue.data[session->queue.front], sizeof(headphones_packet_t));
    session->queue.front = (session->queue.front + 1) % HPCMD_QUEUE_SIZE;
    session->queue.len--;

    pthread_mutex_unlock(&session->queue.mutex);
    return true;
}

static int hpcmd_session_create(hpcmd_session_t *session, const char ipv4[16], const short port) {
    if ((session->sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        syslog(LOG_ERR, "HPCMD socket creating failed, errno=%d, strerror=\"%s\"",
               errno, strerror(errno));
        return -1;
    }

    int enable = 1;
    if (setsockopt(session->sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        syslog(LOG_WARNING, "setsockopt SO_REUSEADDR failed: %s", strerror(errno));
    }

    struct timeval tv = {
        .tv_sec = HPCMD_SOCK_TIMEOUT_MS / 1000,
        .tv_usec = HPCMD_SOCK_TIMEOUT_MS % 1000,
    };
    if (setsockopt(session->sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        syslog(LOG_WARNING, "setsockopt SO_RCVTIMEO failed: %s", strerror(errno));
    }

    memset(&session->addr, 0, sizeof(session->addr));
    session->addr.sin_family = AF_INET;
    session->addr.sin_port = htons(port);
    session->addr.sin_addr.s_addr = htonl(INADDR_ANY);

    session->remote_addr.sin_family = AF_INET;
    session->remote_addr.sin_port = htons(port);
    session->remote_addr.sin_addr.s_addr = inet_addr(ipv4);

    session->sequence = 0;
    session->timestamp = 0;

    if (hpcmd_queue_init(&session->queue) != 0) {
        syslog(LOG_ERR, "HPCMD failed to init session queue");
        close(session->sockfd);
        return -1;
    }

    syslog(LOG_INFO, "HPCMD session created, %s:%d\n", ipv4, port);

    return 0;
}

static void hpcmd_session_destroy(hpcmd_session_t *session) {
    if (session->sockfd > 0) {
        close(session->sockfd);
    }
    hpcmd_queue_destroy(&session->queue);
    memset(session, 0, sizeof(hpcmd_session_t));
}

static void *hpcmd_receiver_task(void *arg) {
    hpcmd_conn_data_t *conn = (hpcmd_conn_data_t *)arg;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    headphones_packet_t packet;
    ssize_t recv_len;

    while (conn->is_running) {
        recv_len = recvfrom(conn->session.sockfd, &packet, sizeof(headphones_packet_t), 0,
                            (struct sockaddr *)&client_addr, &client_len);

        if (recv_len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;  // ignore timeout
            syslog(LOG_ERR, "recvfrom failed: %s", strerror(errno));

            int error = 0;
            socklen_t len = sizeof(error);
            if (getsockopt(conn->session.sockfd, SOL_SOCKET, SO_ERROR, &error, &len) == 0) {
                syslog(LOG_ERR, "Socket error: %d (%s)", error, strerror(error));
            }
            usleep(RECV_ERR_DELAY_US);
            continue;
        }

        if (conn->session.remote_addr.sin_addr.s_addr != client_addr.sin_addr.s_addr) {
            syslog(LOG_WARNING, "HPCMD rejected packet from: %s:%d, size: %zd",
                   inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), recv_len);
            usleep(RECV_ERR_DELAY_US);
            continue;
        }

        if (recv_len != sizeof(headphones_packet_t)) {
            syslog(LOG_WARNING, "HPCMD incorrect packet size: %zd (expected %zu)",
                   recv_len, sizeof(headphones_packet_t));
            usleep(RECV_ERR_DELAY_US);
            continue;
        }

        headphones_packet_t command = {
            .command = packet.command,
            .sequence = ntohs(packet.sequence),
            .timestamp = ntohl(packet.timestamp),
        };

        // TODO: process command
        syslog(LOG_INFO, "HPCMD received command %02x", command.command);
    }

    return NULL;
}

static void *hpcmd_sender_task(void *arg) {
    hpcmd_conn_data_t *conn = (hpcmd_conn_data_t *)arg;
    headphones_packet_t packet;
    ssize_t sent_size;

    while (conn->is_running || conn->session.queue.len > 0) {
        if (!hpcmd_queue_pop(&conn->session, &packet)) {
            continue;
        }
        sent_size = sendto(conn->session.sockfd, &packet, sizeof(headphones_packet_t), 0,
                           (const struct sockaddr *)&conn->session.remote_addr,
                           sizeof(conn->session.remote_addr));

        if (sent_size < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;  // ignore timeout
            syslog(LOG_ERR, "sendto failed: %s", strerror(errno));

            int error = 0;
            socklen_t len = sizeof(error);
            if (getsockopt(conn->session.sockfd, SOL_SOCKET, SO_ERROR, &error, &len) == 0) {
                syslog(LOG_ERR, "Socket error: %d (%s)", error, strerror(error));
            }
            usleep(RECV_ERR_DELAY_US);
            continue;
        }

        if (sent_size != sizeof(headphones_packet_t)) {
            syslog(LOG_WARNING, "HPCMD incorrect packet size: %zd (expected %zu)",
                   sent_size, sizeof(headphones_packet_t));
            usleep(RECV_ERR_DELAY_US);
            continue;
        }

        syslog(LOG_INFO, "HPCMD sent command %02x", packet.command);
    }

    return NULL;
}

void hpcmd_conn_stop(hpcmd_conn_data_t *data) {
    pthread_mutex_lock(&data->mutex);
    data->is_running = false;
    pthread_mutex_unlock(&data->mutex);

    if (data->receiver_tid > 0) {
        pthread_join(data->receiver_tid, NULL);
        data->receiver_tid = 0;
    }

    if (data->sender_tid > 0) {
        pthread_join(data->sender_tid, NULL);
        data->sender_tid = 0;
    }

    pthread_mutex_lock(&data->mutex);
    data->has_active_session = false;
    hpcmd_session_destroy(&data->session);
    pthread_mutex_unlock(&data->mutex);
}

int hpcmd_conn_start(hpcmd_conn_data_t *data, const char ipv4[16]) {
    pthread_mutex_lock(&data->mutex);

    if (data->has_active_session) {
        syslog(LOG_WARNING, "HPCMD has active session");
        pthread_mutex_unlock(&data->mutex);
        return -1;
    }

    if (hpcmd_session_create(&data->session, ipv4, HEADPHONES_CMD_PORT) != 0) {
        syslog(LOG_ERR, "Failed to create HPCMD session");
        pthread_mutex_unlock(&data->mutex);
        hpcmd_conn_stop(data);
        return -1;
    }

    if (pthread_create(&data->receiver_tid, NULL, hpcmd_receiver_task, data) != 0) {
        syslog(LOG_ERR, "Failed to create HPCMD receiver task");
        pthread_mutex_unlock(&data->mutex);
        hpcmd_conn_stop(data);
        return -1;
    }

    if (pthread_create(&data->sender_tid, NULL, hpcmd_sender_task, data) != 0) {
        syslog(LOG_ERR, "Failed to create HPCMD sender task");
        pthread_mutex_unlock(&data->mutex);
        hpcmd_conn_stop(data);
        return -1;
    }

    data->has_active_session = true;
    data->is_running = true;
    pthread_mutex_unlock(&data->mutex);
    return 0;
}

int hpcmd_conn_data_init(hpcmd_conn_data_t *data) {
    memset(data, 0, sizeof(hpcmd_conn_data_t));

    if (pthread_mutex_init(&data->mutex, NULL) != 0) {
        syslog(LOG_ERR, "HPCMD connection mutex init failed");
        return -1;
    }

    data->has_active_session = false;
    data->is_running = false;
    data->receiver_tid = 0;
    data->sender_tid = 0;

    return 0;
}

void hpcmd_conn_data_destroy(hpcmd_conn_data_t *data) {
    hpcmd_conn_stop(data);
    pthread_mutex_destroy(&data->mutex);
}

int hpcmd_send_command(hpcmd_session_t *session, uint8_t command_type) {
    headphones_packet_t command = {
        .command = command_type,
        .sequence = htons(session->sequence++),
        .timestamp = htonl((uint32_t)time(NULL)),
    };
    return hpcmd_queue_push(session, &command);
}
