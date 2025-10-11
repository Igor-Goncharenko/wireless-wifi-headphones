#include "rtp_client.h"

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

int rtp_session_create(rtp_session_t *session, const char *server_ip, const int server_port) {
    if ((session->sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        fprintf(stderr, "RTP socket creating failed, errno=%d, strerror=\"%s\"\n", 
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

    printf("RTP session created, %s:%d\n", server_ip, server_port);

    return 0;
}

void rtp_session_destroy(rtp_session_t *session) {
    if (session->sockfd > 0) {
        close(session->sockfd);
        session->sockfd = -1;
    }
}

static ssize_t rtp_send_packet_small(rtp_session_t *session, const uint8_t *data, 
                                     const size_t data_size, const int marker) {
    uint8_t packet[MAX_PACKET_SIZE];
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

ssize_t rtp_send_packet(rtp_session_t *session, const uint8_t *data, 
                        const size_t data_size, const int marker) {
    if (data_size <= MAX_PACKET_SIZE) {
        return (rtp_send_packet_small(session, data, data_size, marker) != 0) ? 1 : 0;
    }

    size_t total_sent = 0;
    size_t remaining = data_size;
    const uint8_t *current_pos = data;
    int packet_count = 0;

    while (remaining > 0) {
        ssize_t fragment_size = (remaining > MAX_PACKET_SIZE) ?
                                MAX_PACKET_SIZE : remaining;
        
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
