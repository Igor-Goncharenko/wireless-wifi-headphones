#include "headphones_commands.h"

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "config.h"
#include "protocols/headphones.h"

#define RECV_ERR_DELAY_US 100000
#define HPCMD_QUEUE_POP_TIMEOUT_MS 10

#define HPCMD_SOCK_TIMEOUT_MS 1000

static int hpcmd_queue_init(hpcmd_queue_t *q) {
    q->front = 0;
    q->back = 0;
    q->len = 0;
    if (pthread_mutex_init(&q->mutex, NULL) != 0) {
        syslog(LOG_ERR, "HPCMD failed to init session queue mutex");
        return -1;
    }
    if (pthread_cond_init(&q->new_item_cond, NULL) != 0) {
        syslog(LOG_ERR, "HPCMD failed to init session queue condvar");
        pthread_mutex_destroy(&q->mutex);
        return -1;
    }
    return 0;
}

static void hpcmd_queue_destroy(hpcmd_queue_t *q) {
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

    pthread_cond_signal(&session->queue.new_item_cond);
    pthread_mutex_unlock(&session->queue.mutex);
    return 0;
}

static bool hpcmd_queue_pop(hpcmd_session_t *session, headphones_packet_t *dest) {
    pthread_mutex_lock(&session->queue.mutex);

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += HPCMD_QUEUE_POP_TIMEOUT_MS / 1000;
    ts.tv_nsec += (HPCMD_QUEUE_POP_TIMEOUT_MS % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec += ts.tv_nsec / 1000000000;
        ts.tv_nsec %= 1000000000;
    }

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

static bool hpcmd_queue_is_empty(hpcmd_session_t *session) {
    pthread_mutex_lock(&session->queue.mutex);
    const bool is_empty = session->queue.len == 0;
    pthread_mutex_unlock(&session->queue.mutex);
    return is_empty;
}

static int hpcmd_ping_data_init(hpcmd_ping_data_t *data) {
    memset(data, 0, sizeof(hpcmd_ping_data_t));
    data->pack_lost = 0;
    data->pack_recv = 0;
    data->exp_seq = 0;
    data->send_seq = 0;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    data->last_ts = (uint32_t)ts.tv_sec;

    return 0;
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
        .tv_usec = (HPCMD_SOCK_TIMEOUT_MS % 1000) * 1000,
    };
    if (setsockopt(session->sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        syslog(LOG_WARNING, "setsockopt SO_RCVTIMEO failed: %s", strerror(errno));
    }

    memset(&session->addr, 0, sizeof(session->addr));
    session->addr.sin_family = AF_INET;
    session->addr.sin_port = htons(port);
    session->addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(session->sockfd, (struct sockaddr *)&session->addr, sizeof(session->addr)) < 0) {
        syslog(LOG_ERR, "HPCMD server bind failed: %s", strerror(errno));
        close(session->sockfd);
        return -1;
    }

    session->remote_addr.sin_family = AF_INET;
    session->remote_addr.sin_port = htons(port);
    session->remote_addr.sin_addr.s_addr = inet_addr(ipv4);

    session->pack_lost = 0;
    session->pack_recv = 0;
    session->exp_seq = 0;
    session->send_seq = 0;

    if (hpcmd_queue_init(&session->queue) != 0) {
        syslog(LOG_ERR, "HPCMD failed to init session queue");
        close(session->sockfd);
        return -1;
    }

    if (hpcmd_ping_data_init(&session->ping) != 0) {
        syslog(LOG_ERR, "HPCMD failed to init session ping data");
        close(session->sockfd);
        return -1;
    }

    syslog(LOG_INFO, "HPCMD session created, %s:%d\n", ipv4, port);

    return 0;
}

static void hpcmd_session_destroy(hpcmd_session_t *session) {
    if (session->sockfd > 0) {
        close(session->sockfd);
        session->sockfd = -1;
    }
    hpcmd_queue_destroy(&session->queue);
}

static void process_command(hpcmd_conn_data_t *conn, headphones_packet_t command) {
    switch (command.command) {
        case HPCMD_NO_COMMAND:
            break;
        case HPCMD_PING: {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            conn->session.ping.last_ts = (uint32_t)ts.tv_sec;
            }
            break;
        case HPCMD_DISCONNECT:
            rtp_connection_stop(conn->rtp_conn_ptr);
            hpcmd_conn_stop(conn);  // FIXME: thread stops itself
            syslog(LOG_INFO, "Disconnecting from device");
            break;
        default:
            syslog(LOG_WARNING, "Unprocessed headphones command 0x%02x", command.command);
            break;
    }
}

static void update_session_stats(hpcmd_session_t *session, headphones_packet_t *command_ptr) {
    if (command_ptr->command == HPCMD_PING) {   // ping has a separate counter
        if (command_ptr->sequence > session->ping.exp_seq) {
            session->ping.pack_lost += (command_ptr->sequence - session->ping.exp_seq);
        }
        session->ping.exp_seq = command_ptr->sequence + 1;
        session->ping.pack_recv++;

        if (session->ping.pack_recv % 10 == 0) {
            syslog(LOG_INFO, "HPCMD PING received=%" PRIu32 "; lost=%" PRIu32,
                   session->ping.pack_recv, session->ping.pack_lost);
        }
    } else {                                // count other commands
        if (command_ptr->sequence > session->exp_seq) {
            session->pack_lost += (command_ptr->sequence - session->exp_seq);
        }
        session->exp_seq = command_ptr->sequence + 1;
        session->pack_recv++;

        syslog(LOG_INFO, "HPCMD received command 0x%02x", command_ptr->command);
        if (session->pack_recv % 10 == 0) {
            syslog(LOG_INFO, "HPCMD COMMANDS received=%" PRIu32 "; lost=%" PRIu32,
                   session->pack_recv, session->pack_lost);
        }
    }
}

static void *hpcmd_receiver_task(void *arg) {
    hpcmd_conn_data_t *conn = (hpcmd_conn_data_t *)arg;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    headphones_packet_t packet;
    ssize_t recv_len;

    while (atomic_load(&conn->is_running)) {
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

        process_command(conn, command);
        update_session_stats(&conn->session, &command);
    }

    return NULL;
}

static void *hpcmd_sender_task(void *arg) {
    hpcmd_conn_data_t *conn = (hpcmd_conn_data_t *)arg;
    headphones_packet_t packet;
    ssize_t sent_size;

    while (atomic_load(&conn->is_running) || !hpcmd_queue_is_empty(&conn->session)) {
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
    }

    return NULL;
}

static void *hpcmd_ping_task(void *arg) {
    hpcmd_conn_data_t *conn = (hpcmd_conn_data_t *)arg;

    while (atomic_load(&conn->is_running)) {
        headphones_packet_t command = {
            .command = HPCMD_PING,
            .sequence = htons(conn->session.ping.send_seq++),
            .timestamp = htonl((uint32_t)time(NULL)),
        };
        hpcmd_queue_push(&conn->session, &command);
        usleep(PING_INTERVAL_MS * 1000);

        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint32_t now = (uint32_t)ts.tv_sec;
        if (now - conn->session.ping.last_ts > PING_TIMEOUT_S) {
            rtp_connection_stop(conn->rtp_conn_ptr);
            hpcmd_conn_stop(conn);  // FIXME: thread stops itself
            syslog(LOG_INFO, "HPCMD lost connection (ping timeout)");
        }
    }

    return NULL;
}

void hpcmd_conn_stop(hpcmd_conn_data_t *data) {
    atomic_store(&data->is_running, false);

    if (data->receiver_tid > 0) {
        pthread_join(data->receiver_tid, NULL);
        data->receiver_tid = 0;
    }

    if (data->sender_tid > 0) {
        pthread_join(data->sender_tid, NULL);
        data->sender_tid = 0;
    }

    if (data->ping_tid > 0) {
        pthread_join(data->ping_tid, NULL);
        data->ping_tid = 0;
    }

    pthread_mutex_lock(&data->mutex);
    hpcmd_session_destroy(&data->session);
    pthread_mutex_unlock(&data->mutex);
}

int hpcmd_conn_start(hpcmd_conn_data_t *data, const char ipv4[16]) {
    pthread_mutex_lock(&data->mutex);

    if (atomic_load(&data->is_running)) {
        syslog(LOG_WARNING, "HPCMD has active session");
        pthread_mutex_unlock(&data->mutex);
        return -1;
    }

    atomic_store(&data->is_running, true);

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

    if (pthread_create(&data->ping_tid, NULL, hpcmd_ping_task, data) != 0) {
        syslog(LOG_ERR, "Failed to create HPCMD ping task");
        pthread_mutex_unlock(&data->mutex);
        hpcmd_conn_stop(data);
        return -1;
    }

    pthread_mutex_unlock(&data->mutex);
    return 0;
}

int hpcmd_conn_data_init(hpcmd_conn_data_t *data, rtp_connection_data_t *rtp_conn_ptr) {
    if (pthread_mutex_init(&data->mutex, NULL) != 0) {
        syslog(LOG_ERR, "HPCMD connection mutex init failed");
        return -1;
    }

    atomic_store(&data->is_running, false);
    data->receiver_tid = 0;
    data->sender_tid = 0;
    data->ping_tid = 0;

    data->rtp_conn_ptr = rtp_conn_ptr;

    return 0;
}

void hpcmd_conn_data_destroy(hpcmd_conn_data_t *data) {
    hpcmd_conn_stop(data);
    pthread_mutex_destroy(&data->mutex);
}

int hpcmd_send_command(hpcmd_session_t *session, uint8_t command_type) {
    headphones_packet_t command = {
        .command = command_type,
        .sequence = htons(session->send_seq++),
        .timestamp = htonl((uint32_t)time(NULL)),
    };
    return hpcmd_queue_push(session, &command);
}
