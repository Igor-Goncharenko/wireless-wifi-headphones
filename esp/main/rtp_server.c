#include "rtp_server.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include <string.h>

#include "audio.h"
#include "config.h"
#include "event_mgr.h"
#include "protocols/rtp.h"

static const char *TAG = "WHP " __FILE__;
static bool s_running = false;
static TaskHandle_t s_rtp_hndl = NULL;
static rtp_server_t s_server = { 0 };

#define RTP_SOCK_TIMEOUT_MS 1000
#define DELAY_BEFORE_FORCE_TASK_DEL_MS (RTP_SOCK_TIMEOUT_MS + 200)

static int rtp_server_init(void) {
    memset(&s_server, 0, sizeof(rtp_server_t));

    if ((s_server.sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        ESP_LOGE(TAG, "RTP server failed to create socket");
        return -1;
    }

    int enable = 1;
    if (setsockopt(s_server.sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        ESP_LOGW(TAG, "setsockopt SO_REUSEADDR failed");
    }

    memset(&s_server.addr, 0, sizeof(s_server.addr));
    s_server.addr.sin_family = AF_INET;
    s_server.addr.sin_addr.s_addr = inet_addr(g_event_mgr.host_ip4);
    s_server.addr.sin_port = htons(RTP_PORT);

    if (bind(s_server.sockfd, (struct sockaddr *)&s_server.addr, sizeof(s_server.addr)) < 0) {
        ESP_LOGE(TAG, "RTP server bind failed");
        close(s_server.sockfd);
        s_server.sockfd = -1;
        return -1;
    }

    struct timeval tv = {
        .tv_sec = RTP_SOCK_TIMEOUT_MS / 1000,
        .tv_usec = RTP_SOCK_TIMEOUT_MS % 1000,
    };
    if (setsockopt(s_server.sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        ESP_LOGW(TAG, "setsockopt SO_RCVTIMEO failed: %s", strerror(errno));
    }

    s_server.expected_sequence = 0;
    s_server.packets_received = 0;
    s_server.packets_lost = 0;

    s_server.rb = get_rb_ptr();

    ESP_LOGI(TAG, "RTP server initialized: sockfd=%d, port=%d", s_server.sockfd, RTP_PORT);
    return 0;
}

static void rtp_server_destroy() {
    if (s_server.sockfd > 0) {
        close(s_server.sockfd);
    }

    memset(&s_server, 0, sizeof(rtp_server_t));
    ESP_LOGI(TAG, "RTP server destroyed");
}

static void rtp_receiver_task(void *arg) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    uint8_t buffer[PACKET_SIZE];
    ssize_t recv_len;

    ESP_LOGI(TAG, "RTP server started on port %d", RTP_PORT);
    
    while (s_running) {
        recv_len = recvfrom(s_server.sockfd, buffer, sizeof(buffer), 0,
                            (struct sockaddr *)&client_addr, &client_len);
        
        if (recv_len < 0) {
            if (errno == EAGAIN) continue;  // ignore timeout
            ESP_LOGE(TAG, "recvfrom failed: %s", strerror(errno));

            int error = 0;
            socklen_t len = sizeof(error);
            if (getsockopt(s_server.sockfd, SOL_SOCKET, SO_ERROR, &error, &len) == 0) {
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
            if (s_server.expected_sequence != 0 && sequence != s_server.expected_sequence) {
                s_server.packets_lost += (sequence - s_server.expected_sequence);
            }
            s_server.expected_sequence = sequence + 1;
            s_server.packets_received++;
            
            const uint8_t *audio_data = buffer + sizeof(rtp_header_t);
            const size_t audio_data_size = recv_len - sizeof(rtp_header_t);
            UBaseType_t res = xRingbufferSend(*s_server.rb, audio_data, audio_data_size,
                                              pdMS_TO_TICKS(100));
            if (res != pdTRUE) {
                ESP_LOGW(TAG, "Ring buffer full, dropped %d bytes", audio_data_size);
            }
            if (s_server.packets_received % 100 == 0) {
                ESP_LOGI(TAG, "Received %lu packets, lost: %lu", 
                         s_server.packets_received, s_server.packets_lost);
            }
        }
    }
    
    ESP_LOGI(TAG, "RTP server stopped");
    s_rtp_hndl = NULL;
    vTaskDelete(NULL);
}

void rtp_server_mgr_task(void *arg) {
    while (1) {
        xEventGroupWaitBits(
            g_event_mgr.signals,
            SIG_START_RTP,
            pdTRUE,
            pdTRUE,
            portMAX_DELAY
        );

        if (rtp_server_init() != 0) {
            ESP_LOGE(TAG, "Failed to init rtp server");
            xEventGroupSetBits(g_event_mgr.events, EV_RTP_INIT_FAILED);
            continue;
        }

        s_running = true;
        xTaskCreate(rtp_receiver_task, "rtp_receiver_task", 4096, NULL, 5, &s_rtp_hndl);

        xEventGroupWaitBits(
            g_event_mgr.signals,
            SIG_STOP_RTP,
            pdTRUE,
            pdTRUE,
            portMAX_DELAY
        );

        s_running = false;
        vTaskDelay(pdMS_TO_TICKS(DELAY_BEFORE_FORCE_TASK_DEL_MS));
        if (s_rtp_hndl != NULL) {
            vTaskDelete(s_rtp_hndl);
            s_rtp_hndl = NULL;
            ESP_LOGW(TAG, "RTP task did not stop properly, forcing stop");
        }
        rtp_server_destroy();
    }
}
