#include "discovery_server.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif_ip_addr.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <stdbool.h>
#include <time.h>

#include "config.h"
#include "wifi.h"
#include "protocols/discovery.h"
#include "event_mgr.h"

static const char *TAG = "WHP " __FILE__;

headphones_info_t g_device_info = {
    .name = CONFIG_HEADPHONES_NAME,
    .audio = {
        .bit_width = AUDIO_SAMPLE_SIZE,
        .sample_rate = htons(AUDIO_SAMPLE_RATE),
        .channels = AUDIO_CHANNELS,
    },
};

static whitelist_entry_t s_whitelist[MAX_WHITELIST_SIZE];
static int s_whitelist_len = 0;
static SemaphoreHandle_t s_whitelist_mutex = NULL;

static void init_device_info(void) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    snprintf(g_device_info.mac, sizeof(g_device_info.mac),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    strncpy(g_device_info.ipv4, g_ip4_str, sizeof(g_device_info.ipv4) - 1);

    ESP_LOGI(TAG, "Device: name = \"%s\"; mac=\"%s\"; ipv4=\"%s\";", g_device_info.name,
             g_device_info.mac, g_device_info.ipv4);
}

static void update_whitelist(const struct in_addr addr) {
    if (xSemaphoreTake(s_whitelist_mutex, pdMS_TO_TICKS(100)) == pdFALSE) {
        ESP_LOGW(TAG, "Failed to take whitelist mutex");
        return;
    }

    const time_t now = time(NULL);
    int i = 0;
    bool addr_registered = false;

    while (i < s_whitelist_len) {
        if (memcmp(&s_whitelist[i].addr, &addr, sizeof(struct in_addr)) == 0) {
            addr_registered = true;
            s_whitelist[i].timestamp = now;
        }
        if (now - s_whitelist[i].timestamp > WHITELIST_TIMEOUT) {
            s_whitelist_len--;
            if (i != s_whitelist_len) {
                memcpy(&s_whitelist[i], &s_whitelist[s_whitelist_len - 1], sizeof(whitelist_entry_t));
            }
            i--;
        }
        i++;
    }

    if (!addr_registered && s_whitelist_len < MAX_WHITELIST_SIZE) {
        s_whitelist[s_whitelist_len].timestamp = now;
        s_whitelist[s_whitelist_len].addr = addr;
        s_whitelist_len++;
    }

    char addr_str[INET_ADDRSTRLEN];
    for (int i = 0; i < s_whitelist_len; i++) {
        inet_ntoa_r(s_whitelist[i].addr, addr_str, sizeof(addr_str));
        ESP_LOGI(TAG, "%d) %s", i + 1, addr_str);
    }

    xSemaphoreGive(s_whitelist_mutex);
}

static void discovery_server_task(void) {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[128];
    int recv_len;

    init_device_info();
    
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        return;
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(DISCOVERY_PORT);
    
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Bind failed");
        close(sockfd);
        return;
    }
    
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(MULTICAST_GROUP);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    
    if (setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        ESP_LOGE(TAG, "Multicast group join failed");
        close(sockfd);
        return;
    }
    
    ESP_LOGI(TAG, "Discovery server started on port %d", DISCOVERY_PORT);
    
    while (1) {
        EventBits_t bits = xEventGroupGetBits(g_system_events);
        if (bits & EVENT_CLIENT_CONNECTED) {
            ESP_LOGI(TAG, "Client connected, stopping discovery");
            break;
        }

        recv_len = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0,
                            (struct sockaddr *)&client_addr, &client_len);
        
        if (recv_len > 0) {
            buffer[recv_len] = '\0';
            ESP_LOGI(TAG, "received discovery request: \"%s\"", buffer);

            if (strcmp(buffer, DISCOVERY_REQUEST) == 0) {
                if (sendto(sockfd, &g_device_info, sizeof(headphones_info_t), 0,
                          (struct sockaddr *)&client_addr, client_len) < 0) {
                    ESP_LOGE(TAG, "Failed to send response");
                } else {
                    update_whitelist(client_addr.sin_addr);
                }
            }
        } else if (recv_len < 0) {
            ESP_LOGE(TAG, "recvfrom failed: errno=%d", errno);
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    close(sockfd);
}

void discovery_server_mgr_task(void *arg) {
    EventBits_t bits;
    s_whitelist_mutex = xSemaphoreCreateMutex();
    if (s_whitelist_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create whitelist mutex");
        return;
    }

    while (1) {
        bits = xEventGroupWaitBits(
            g_system_events,
            EVENT_WIFI_CONNECTED | EVENT_DISCOVERY_START,
            pdFALSE,
            pdTRUE,
            portMAX_DELAY
        );

        xEventGroupClearBits(g_system_events, EVENT_DISCOVERY_START);

        ESP_LOGI(TAG, "discovery_server_task started");
        discovery_server_task();
        ESP_LOGI(TAG, "discovery_server_task stopped");

        bits = xEventGroupGetBits(g_system_events);
        if (!(bits & EVENT_CLIENT_CONNECTED)) {
            ESP_LOGW(TAG, "Discovery stopped without client connection");
            xEventGroupSetBits(g_system_events, EVENT_DISCOVERY_START);
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }

    vSemaphoreDelete(s_whitelist_mutex);
}
