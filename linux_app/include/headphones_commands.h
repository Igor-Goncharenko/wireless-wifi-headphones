#ifndef HEADPHONES_COMMANDS_H
#define HEADPHONES_COMMANDS_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <arpa/inet.h>

typedef struct {
    int sockfd;
    struct sockaddr_in addr;
    struct sockaddr_in remote_addr;

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
} hpcmd_conn_data_t;

int hpcmd_conn_data_init(hpcmd_conn_data_t *data);

void hpcmd_conn_data_destroy(hpcmd_conn_data_t *data);

int hpcmd_conn_start(hpcmd_conn_data_t *data, const char ipv4[16]);

void hpcmd_conn_stop(hpcmd_conn_data_t *data);

#endif /* HEADPHONES_COMMANDS_H */
