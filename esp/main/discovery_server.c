#include "discovery_server.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"
#include <stdio.h>

#include "wifi.h"

static const char *TAG = "WHP " __FILE__;

device_info_t g_device_info;

static void init_device_info(void) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    
    snprintf(g_device_info.device_id, sizeof(g_device_info.device_id),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    strlcpy(g_device_info.model, "WiFi Headphones v1.0", sizeof(g_device_info.model));
    strcpy(g_device_info.ip_addr, g_ip4_str);
    
    ESP_LOGI(TAG, "Device: ID=\"%s\"; Model=\"\"", g_device_info.device_id, g_device_info.model);
}

static int create_discovery_response(char *buffer, const size_t buffer_size) {
    return snprintf(buffer, buffer_size,
                   "{\"type\":\"HEADPHONES_RESPONSE\","
                   "\"model\":\"%s\","
                   "\"id\":\"%s\","
                   "\"ip\":\"%s\"}",
                   g_device_info.model, g_device_info.device_id, g_device_info.ip_addr);
}

void discovery_server_task(void *args) {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[128];
    char response[256];
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
    server_addr.sin_port = htons(CONFIG_DISCOVERY_PORT);
    
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Bind failed");
        close(sockfd);
        vTaskDelete(NULL);
        return;
    }
    
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(CONFIG_MULTICAST_GROUP);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    
    if (setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        ESP_LOGE(TAG, "Multicast group join failed");
        close(sockfd);
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "Discovery server started on port %d", CONFIG_DISCOVERY_PORT);
    
    while (1) {
        recv_len = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0,
                           (struct sockaddr *)&client_addr, &client_len);
        
        if (recv_len > 0) {
            buffer[recv_len] = '\0';
            //ESP_LOGI(TAG, "Received: %s from " IPSTR, buffer, 
            //         IP2STR(&client_addr.sin_addr.s_addr));
            
            if (strcmp(buffer, CONFIG_DISCOVERY_REQUEST) == 0) {
                int response_len = create_discovery_response(response, sizeof(response));
               
                if (sendto(sockfd, response, response_len, 0,
                          (struct sockaddr *)&client_addr, client_len) < 0) {
                    ESP_LOGE(TAG, "Failed to send response");
                } else {
                    //ESP_LOGI(TAG, "Discovery response sent to " IPSTR, 
                    //         IP2STR(&client_addr.sin_addr.s_addr));
                }
            }
        } else if (recv_len < 0) {
            ESP_LOGE(TAG, "recvfrom failed: errno=%d", errno);
        }
        
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    
    close(sockfd);
    vTaskDelete(NULL);
}
