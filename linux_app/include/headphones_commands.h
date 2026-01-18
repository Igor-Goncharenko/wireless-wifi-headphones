#ifndef HEADPHONES_COMMANDS_H
#define HEADPHONES_COMMANDS_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <arpa/inet.h>

#include "protocols/headphones.h"
#include "rtp_client.h"

#define HPCMD_QUEUE_SIZE 16

typedef struct {
    headphones_packet_t data[HPCMD_QUEUE_SIZE];
    size_t front;
    size_t back;
    size_t len;

    pthread_mutex_t mutex;
    pthread_cond_t new_item_cond;
} hpcmd_queue_t;

typedef struct {
    int sockfd;
    struct sockaddr_in addr;
    struct sockaddr_in remote_addr;

    hpcmd_queue_t queue;

    uint16_t sequence;
    uint32_t timestamp;
} hpcmd_session_t;

typedef struct {
    bool has_active_session;
    bool is_running;

    pthread_t sender_tid;
    pthread_t receiver_tid;
    pthread_mutex_t mutex;

    hpcmd_session_t session;
    rtp_connection_data_t *rtp_conn_ptr;    // used to disconnect client
} hpcmd_conn_data_t;

int hpcmd_conn_data_init(hpcmd_conn_data_t *data);

void hpcmd_conn_data_destroy(hpcmd_conn_data_t *data);

int hpcmd_conn_start(hpcmd_conn_data_t *data, const char ipv4[16]);

void hpcmd_conn_stop(hpcmd_conn_data_t *data);

int hpcmd_send_command(hpcmd_session_t *session, uint8_t command_type);

#endif /* HEADPHONES_COMMANDS_H */
