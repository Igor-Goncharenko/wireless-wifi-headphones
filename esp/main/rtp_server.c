#include "rtp_server.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"
#include <string.h>

#include "config.h"
#include "protocols/rtp.h"

static const char *TAG = "WHP " __FILE__;

#define PACKET_BUFFER_SIZE (CONFIG_RTP_PACKET_SIZE + sizeof(rtp_header_t) + 1)

int rtp_server_init(rtp_server_t *rtp_ser, const RingbufHandle_t rb) {
    memset(rtp_ser, 0, sizeof(rtp_server_t));

    if ((rtp_ser->sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        ESP_LOGE(TAG, "RTP server failed to create socket");
        return -1;
    }

    int enable = 1;
    if (setsockopt(rtp_ser->sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        ESP_LOGW(TAG, "setsockopt SO_REUSEADDR failed");
    }

    memset(&rtp_ser->server_addr, 0, sizeof(rtp_ser->server_addr));
    rtp_ser->server_addr.sin_family = AF_INET;
    rtp_ser->server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    rtp_ser->server_addr.sin_port = htons(RTP_PORT);

    if (bind(rtp_ser->sockfd, (struct sockaddr *)&rtp_ser->server_addr, sizeof(rtp_ser->server_addr)) < 0) {
        ESP_LOGE(TAG, "RTP server bind failed");
        close(rtp_ser->sockfd);
        rtp_ser->sockfd = -1;
        return -1;
    }

    rtp_ser->expected_sequence = 0;
    rtp_ser->packets_received = 0;
    rtp_ser->packets_lost = 0;

    rtp_ser->rb = rb;

    ESP_LOGI(TAG, "RTP server initialized: sockfd=%d, port=%d", rtp_ser->sockfd, RTP_PORT);
    return 0;
}

void rtp_server_destroy(rtp_server_t *rtp_ser) {
    if (rtp_ser->sockfd > 0) {
        close(rtp_ser->sockfd);
    }

    memset(rtp_ser, 0, sizeof(rtp_server_t));
    ESP_LOGI(TAG, "RTP server destroyed");
}

void rtp_receiver_task(void *arg) {
    rtp_server_t *server = (rtp_server_t*)arg;

    if (server == NULL || server->sockfd < 0 || server->rb == NULL) {
        ESP_LOGE(TAG, "Invalid server state");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    uint8_t buffer[PACKET_SIZE];
    ssize_t recv_len;

    ESP_LOGI(TAG, "RTP server started on port %d", RTP_PORT);
    
    while (1) {
        recv_len = recvfrom(server->sockfd, buffer, sizeof(buffer), 0,
                            (struct sockaddr *)&client_addr, &client_len);
        
        if (recv_len < 0) {
            int err = errno;
            ESP_LOGE(TAG, "recvfrom failed: error %d (%s), sockfd=%d", 
                     err, strerror(err), server->sockfd);

            int error = 0;
            socklen_t len = sizeof(error);
            if (getsockopt(server->sockfd, SOL_SOCKET, SO_ERROR, &error, &len) == 0) {
                ESP_LOGE(TAG, "Socket error: %d (%s)", error, strerror(error));
            }
            
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (recv_len >= (ssize_t)sizeof(rtp_header_t)) {
            const rtp_header_t *header = (rtp_header_t*)buffer;
            
            if (header->ver != 2) {
                ESP_LOGW(TAG, "Invalid RTP version");
                continue;
            }
            if (header->payload_types != RTP_PAYLOAD_TYPE) {
                ESP_LOGW(TAG, "Unexpected payload type: %d", header->payload_types);
                continue;
            }
            
            const uint16_t sequence = ntohs(header->sequence);
            
            // check packet loss
            if (server->expected_sequence != 0 && sequence != server->expected_sequence) {
                server->packets_lost += (sequence - server->expected_sequence);
            }
            server->expected_sequence = sequence + 1;
            server->packets_received++;
            
            const uint8_t *audio_data = buffer + sizeof(rtp_header_t);
            const size_t audio_data_size = recv_len - sizeof(rtp_header_t);
            UBaseType_t res = xRingbufferSend(server->rb, audio_data, audio_data_size,
                                              pdMS_TO_TICKS(100));
            if (res != pdTRUE) {
                ESP_LOGW(TAG, "Ring buffer full, dropped %d bytes", audio_data_size);
            }
            if (server->packets_received % 100 == 0) {
                ESP_LOGI(TAG, "Received %lu packets, lost: %lu", 
                         server->packets_received, server->packets_lost);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    ESP_LOGI(TAG, "RTP server stopped");
    vTaskDelete(NULL);
}
