#include "discovery_server.h"

#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <stdbool.h>

#include "config.h"
#include "wifi.h"
#include "protocols/discovery.h"
#include "event_mgr.h"

static const char *TAG = "WHP " __FILE__;
static TaskHandle_t s_handshake_hndl = NULL;
static TaskHandle_t s_discovery_hndl = NULL;
static volatile bool s_running = false;

headphones_info_t g_device_info = {
    .name = CONFIG_HEADPHONES_NAME,
    .audio = {
        .bit_width = AUDIO_SAMPLE_SIZE,
        .sample_rate = htons(AUDIO_SAMPLE_RATE),
        .channels = AUDIO_CHANNELS,
    },
};

static void init_device_info(void) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    snprintf(g_device_info.mac, sizeof(g_device_info.mac),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    strncpy(g_device_info.ipv4, wifi_get_ip4_str(), sizeof(g_device_info.ipv4) - 1);

    ESP_LOGI(TAG, "Device: name = \"%s\"; mac=\"%s\"; ipv4=\"%s\";", g_device_info.name,
             g_device_info.mac, g_device_info.ipv4);
}

static void discovery_server_task(void *arg) {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[128];
    int recv_len;

    init_device_info();
    
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        vTaskDelete(NULL);
        return;
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(DISCOVERY_PORT);
    
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Bind failed");
        close(sockfd);
        vTaskDelete(NULL);
        return;
    }
    
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(MULTICAST_GROUP);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    
    if (setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        ESP_LOGE(TAG, "Multicast group join failed");
        close(sockfd);
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "Discovery server started on port %d", DISCOVERY_PORT);
    
    while (s_running) {
        recv_len = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0,
                            (struct sockaddr *)&client_addr, &client_len);
        
        if (recv_len > 0) {
            buffer[recv_len] = '\0';
            ESP_LOGI(TAG, "received discovery request: \"%s\"", buffer);

            if (strcmp(buffer, DISCOVERY_REQUEST) == 0) {
                if (sendto(sockfd, &g_device_info, sizeof(headphones_info_t), 0,
                          (struct sockaddr *)&client_addr, client_len) < 0) {
                    ESP_LOGE(TAG, "Failed to send response");
                }
            }
        } else if (recv_len < 0) {
            ESP_LOGE(TAG, "recvfrom failed: errno=%d", errno);
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    close(sockfd);
    ESP_LOGI(TAG, "Discovery server stopped");
    vTaskDelete(NULL);
}

static void handshake_server_task(void *arg) {
    int sockfd;
    char buffer[128];
    int recv_len;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(HANDSHAKE_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Bind failed: errno %d", errno);
        close(sockfd);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Handshake task started");

    while (s_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr,
                            &client_len);

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));

        if (recv_len > 0) {
            buffer[recv_len] = '\0';
            ESP_LOGI(TAG, "received handshake request: \"%s\"", buffer);

            if (strcmp(buffer, HANDSHAKE_REQUEST) == 0) {
                ssize_t bytes_sent = sendto(sockfd, HANDSHAKE_RESPONSE, sizeof(HANDSHAKE_RESPONSE),
                                            0, (struct sockaddr *)&client_addr, client_len);
                if (bytes_sent < 0) {
                    ESP_LOGE(TAG, "HANDSHAKE_RESPONSE send failed");
                }

                if (xSemaphoreTake(g_event_mgr.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    xEventGroupSetBits(g_event_mgr.events, EV_CLIENT_CONNECTED);
                    char* ip_addr_str = inet_ntoa(client_addr.sin_addr);
                    strncpy(g_event_mgr.host_ip4, ip_addr_str, IP4ADDR_STRLEN_MAX);
                    ESP_LOGI(TAG, "Connection accepted from %s", client_ip);
                    xSemaphoreGive(g_event_mgr.mutex);
                } else {
                    ESP_LOGW(TAG, "Failed to lock g_conn_cfg mutex");
                }
            }
        } else if (recv_len < 0) {
            ESP_LOGE(TAG, "recvfrom failed: errno=%d", errno);
        }
    }

    close(sockfd);
    ESP_LOGI(TAG, "Handshake task stopped");
    vTaskDelete(NULL);
}

void discovery_server_mgr_task(void *arg) {
    while (1) {
        xEventGroupWaitBits(
            g_event_mgr.signals,
            SIG_START_DISCOVERY,
            pdTRUE,
            pdTRUE,
            portMAX_DELAY
        );

        s_running = true;
        xTaskCreate(handshake_server_task, "handshake_server_task", 4096, NULL, 5, &s_handshake_hndl);
        xTaskCreate(discovery_server_task, "discovery_server_task", 4096, NULL, 5, &s_discovery_hndl);

        xEventGroupWaitBits(
            g_event_mgr.signals,
            SIG_STOP_DISCOVERY,
            pdTRUE,
            pdTRUE,
            portMAX_DELAY
        );

        s_running = false;
        vTaskDelay(pdMS_TO_TICKS(500));
        vTaskDelete(s_handshake_hndl);
        vTaskDelete(s_discovery_hndl);
    }
}
