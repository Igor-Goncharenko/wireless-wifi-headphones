#include "headphones_commands.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/socket.h>

#include "config.h"
#include "protocols/headphones.h"

#define RECV_ERR_DELAY_US 100000

static int hpcmd_session_create(hpcmd_session_t *session, const char ipv4[16], const short port) {
    if ((session->sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        syslog(LOG_ERR, "HPCMD socket creating failed, errno=%d, strerror=\"%s\"",
               errno, strerror(errno));
        return -1;
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

    syslog(LOG_INFO, "HPCMD session created, %s:%d\n", ipv4, port);

    return 0;
}

static void hpcmd_session_destroy(hpcmd_session_t *session) {
    if (session->sockfd > 0) {
        close(session->sockfd);
    }
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

    while (conn->is_running) {

    }

    return NULL;
}

void hpcmd_conn_stop(hpcmd_conn_data_t *data) {
    pthread_mutex_lock(&data->mutex);
    data->is_running = false;
    pthread_mutex_unlock(&data->mutex);

    if (data->sender_tid > 0) {
        pthread_join(data->sender_tid, NULL);
        data->sender_tid = 0;
    }
    if (data->receiver_tid > 0) {
        pthread_join(data->receiver_tid, NULL);
        data->receiver_tid = 0;
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

    if (pthread_create(&data->sender_tid, NULL, hpcmd_sender_task, data) != 0) {
        syslog(LOG_ERR, "Failed to create HPCMD sender task");
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
