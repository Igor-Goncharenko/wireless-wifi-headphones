#include "rtp_client.h"

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <syslog.h>

#include "ringbuf.h"

static int rtp_session_create(rtp_session_t *session, const char *server_ip, const int server_port) {
    if ((session->sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        syslog(LOG_ERR, "RTP socket creating failed, errno=%d, strerror=\"%s\"", 
                errno, strerror(errno));
        return -1;
    }

    memset(&session->server_addr, 0, sizeof(session->server_addr));
    session->server_addr.sin_family = AF_INET;
    session->server_addr.sin_port = htons(server_port);
    session->server_addr.sin_addr.s_addr = inet_addr(server_ip);

    session->sequence = rand() % 0xFFFF;
    session->timestamp = 0;
    session->ssrc = rand();

    syslog(LOG_INFO, "RTP session created, %s:%d\n", server_ip, server_port);

    return 0;
}

static void rtp_session_destroy(rtp_session_t *session) {
    if (session->sockfd > 0) {
        close(session->sockfd);
        session->sockfd = -1;
    }
}

static ssize_t rtp_send_packet_small(rtp_session_t *session, const uint8_t *data, 
                                     const size_t data_size, const int marker) {
    uint8_t packet[PACKET_SIZE];
    rtp_header_t *header = (rtp_header_t*) packet;

    header->ver = RTP_VERSION;
    header->p = 0;
    header->x = 0;

    header->m = marker;
    header->payload_types = RTP_PAYLOAD_TYPE;
    header->sequence = htons(session->sequence++);
    header->timestamp = htonl(session->timestamp);
    header->ssrc = htonl(session->ssrc);

    memcpy(packet + sizeof(rtp_header_t), data, data_size);

    ssize_t sent = sendto(session->sockfd, packet, sizeof(rtp_header_t) + data_size, 0,
            (struct sockaddr*)&session->server_addr, sizeof(session->server_addr));

    if (sent > 0) {
        session->timestamp += data_size / (SAMPLE_SIZE * CHANNELS);
    }

    return sent;
}

static ssize_t rtp_send_packet(rtp_session_t *session, const uint8_t *data, 
                        const size_t data_size, const int marker) {
    if (data_size <= PACKET_SIZE) {
        return (rtp_send_packet_small(session, data, data_size, marker) != 0) ? 1 : 0;
    }

    size_t total_sent = 0;
    size_t remaining = data_size;
    const uint8_t *current_pos = data;
    int packet_count = 0;

    while (remaining > 0) {
        ssize_t fragment_size = (remaining > PACKET_SIZE) ?
                                PACKET_SIZE : remaining;
        
        const int fragment_marker = (remaining == fragment_size) ? marker : 0;
        
        const ssize_t sent = rtp_send_packet_small(session, current_pos, fragment_size, fragment_marker);
        
        if (sent <= 0) return (total_sent > 0) ? total_sent : -1;
        
        const ssize_t payload_sent = sent - sizeof(rtp_header_t);
        total_sent += payload_sent;
        current_pos += payload_sent;
        remaining -= payload_sent;
        packet_count++;
        
        usleep(1000);
    }
    
    return packet_count;
}

static void *rtp_sender_task(void *arg) {
    rtp_connection_data_t *conn_data = (rtp_connection_data_t*)arg;
    uint8_t data[FRAMES_PER_PACKET];
    size_t read;

    while (conn_data->is_running) {
        pthread_mutex_lock(&conn_data->mutex);

        read = ringbuf_read_block(conn_data->rb_ptr, data, FRAMES_PER_PACKET);
        rtp_send_packet(&conn_data->active_session, data, read, 0);

        pthread_mutex_unlock(&conn_data->mutex);

        usleep(1000);
    }

    return NULL;
}

int rtp_connection_start(rtp_connection_data_t *data, const char ipv4[16]) {
    pthread_mutex_lock(&data->mutex);

    if (data->has_active_session) {
        syslog(LOG_WARNING, "Already connected to device");
        pthread_mutex_unlock(&data->mutex);
        return -1;
    }

    if (rtp_session_create(&data->active_session, ipv4, RTP_PORT) != 0) {
        syslog(LOG_ERR, "Failed to create RTP session");
        pthread_mutex_unlock(&data->mutex);
        return -1;
    }

    ringbuf_cleanup(data->rb_ptr);

    if (pthread_create(&data->tid, NULL, rtp_sender_task, data) != 0) {
        syslog(LOG_ERR, "Failed to create thread for rtp_sender_task");
        rtp_session_destroy(&data->active_session);
        pthread_mutex_unlock(&data->mutex);
        return -1;
    }

    data->has_active_session = true;
    data->is_running = true;
    strncpy(data->ipv4, ipv4, 15);

    pthread_mutex_unlock(&data->mutex);

    return 0;
}

void rtp_connection_stop(rtp_connection_data_t *data) {
    if (data->has_active_session) {
        pthread_mutex_lock(&data->mutex);
        data->is_running = false;
        pthread_mutex_unlock(&data->mutex);

        pthread_join(data->tid, NULL);

        pthread_mutex_lock(&data->mutex);
        data->has_active_session = false;
        rtp_session_destroy(&data->active_session);
        pthread_mutex_unlock(&data->mutex);
    }
}

int rtp_connection_data_init(rtp_connection_data_t *data, ringbuf_t *rb_ptr) {
    if (pthread_mutex_init(&data->mutex, NULL) != 0) {
        syslog(LOG_ERR, "RTP connection mutex init failed");
        return -1;
    }
    data->is_running = false;
    data->has_active_session = false;
    data->rb_ptr = rb_ptr;

    return 0;
}

void rtp_connection_data_destroy(rtp_connection_data_t *data) {
    rtp_connection_stop(data);
    pthread_mutex_destroy(&data->mutex);
}
