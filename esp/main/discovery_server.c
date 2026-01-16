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
#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>

#include "config.h"
#include "wifi.h"
#include "protocols/discovery.h"
#include "event_mgr.h"

#define DISCOVERY_SOCK_TIMEOUT_MS 1000
#define DELAY_BEFORE_FORCE_TASK_DEL_MS (DISCOVERY_SOCK_TIMEOUT_MS + 200)

static const char *TAG = "WHP " __FILE__;
static TaskHandle_t s_handshake_hndl = NULL;
static TaskHandle_t s_discovery_hndl = NULL;
static bool s_running = false;

static int s_discovery_sockfd = -1;
static int s_handshake_sockfd = -1;

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

static int discovery_server_init(void) {
    struct sockaddr_in server_addr;

    if ((s_discovery_sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        ESP_LOGE(TAG, "Failed to create socket: %s", strerror(errno));
        s_discovery_sockfd = -1;
        return -1;
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(DISCOVERY_PORT);
    
    if (bind(s_discovery_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Bind failed: %s", strerror(errno));
        close(s_discovery_sockfd);
        s_discovery_sockfd = -1;
        return -1;
    }
    
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(MULTICAST_GROUP);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    
    if (setsockopt(s_discovery_sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        ESP_LOGE(TAG, "Multicast group join failed: %s", strerror(errno));
        close(s_discovery_sockfd);
        s_discovery_sockfd = -1;
        return -1;
    }

    struct timeval tv = {
        .tv_sec = DISCOVERY_SOCK_TIMEOUT_MS / 1000,
        .tv_usec = DISCOVERY_SOCK_TIMEOUT_MS % 1000,
    };
    if (setsockopt(s_discovery_sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        ESP_LOGW(TAG, "setsockopt SO_RCVTIMEO failed: %s", strerror(errno));
    }

    ESP_LOGI(TAG, "Discovery server initialized successfully");
    return 0;
}

static void discovery_server_destroy(void) {
    if (s_discovery_sockfd > 0) {
        close(s_discovery_sockfd);
        s_discovery_sockfd = -1;
    }
}

static void discovery_server_task(void *arg) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[128];
    int recv_len;

    init_device_info();

    ESP_LOGI(TAG, "Discovery server started on port %d", DISCOVERY_PORT);

    while (s_running) {
        recv_len = recvfrom(s_discovery_sockfd, buffer, sizeof(buffer) - 1, 0,
                            (struct sockaddr *)&client_addr, &client_len);
        
        if (recv_len > 0) {
            buffer[recv_len] = '\0';
            ESP_LOGI(TAG, "received discovery request: \"%s\"", buffer);

            if (strcmp(buffer, DISCOVERY_REQUEST) == 0) {
                if (sendto(s_discovery_sockfd, &g_device_info, sizeof(headphones_info_t), 0,
                          (struct sockaddr *)&client_addr, client_len) < 0) {
                    ESP_LOGE(TAG, "Failed to send response");
                }
            }
        } else if (recv_len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {   // ignore timeout
            ESP_LOGE(TAG, "recvfrom failed: errno=%d, strerror=\"%s\"", errno, strerror(errno));
        }
    }
    
    ESP_LOGI(TAG, "Discovery server stopped");
    s_discovery_hndl = NULL;
    vTaskDelete(NULL);
}

static int handshake_server_init(void) {
    if ((s_handshake_sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        ESP_LOGE(TAG, "Failed to create socket: %s", strerror(errno));
        return -1;
    }

    int enable = 1;
    if (setsockopt(s_handshake_sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        ESP_LOGW(TAG, "setsockopt SO_REUSEADDR failed: %s", strerror(errno));
    }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(HANDSHAKE_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (bind(s_handshake_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Bind failed: %s", strerror(errno));
        close(s_handshake_sockfd);
        return -1;
    }

    struct timeval tv = {
        .tv_sec = DISCOVERY_SOCK_TIMEOUT_MS / 1000,
        .tv_usec = DISCOVERY_SOCK_TIMEOUT_MS % 1000,
    };
    if (setsockopt(s_handshake_sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        ESP_LOGW(TAG, "setsockopt SO_RCVTIMEO failed: %s", strerror(errno));
    }

    ESP_LOGI(TAG, "Handshake server initialized successfully");
    return 0;
}

static void handshake_server_destroy(void) {
    if (s_handshake_sockfd > 0) {
        close(s_handshake_sockfd);
        s_handshake_sockfd = -1;
    }
}

static void handshake_server_task(void *arg) {
    char buffer[128];
    int recv_len;

    ESP_LOGI(TAG, "Handshake task started");

    while (s_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        recv_len = recvfrom(s_handshake_sockfd, buffer, sizeof(buffer), 0,
                            (struct sockaddr *)&client_addr, &client_len);

        if (recv_len > 0) {
            buffer[recv_len] = '\0';
            ESP_LOGI(TAG, "received handshake request: \"%s\"", buffer);

            if (strcmp(buffer, HANDSHAKE_REQUEST) == 0) {
                ssize_t bytes_sent = sendto(s_handshake_sockfd, HANDSHAKE_RESPONSE,
                                            sizeof(HANDSHAKE_RESPONSE), 0,
                                            (struct sockaddr *)&client_addr, client_len);
                if (bytes_sent < 0) {
                    ESP_LOGE(TAG, "HANDSHAKE_RESPONSE send failed");
                }

                if (xSemaphoreTake(g_event_mgr.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    xEventGroupSetBits(g_event_mgr.events, EV_CLIENT_CONNECTED);
                    inet_ntop(AF_INET, &client_addr.sin_addr, g_event_mgr.host_ip4,
                              sizeof(g_event_mgr.host_ip4));
                    ESP_LOGI(TAG, "Connection accepted from %s", g_event_mgr.host_ip4);
                    xSemaphoreGive(g_event_mgr.mutex);
                } else {
                    ESP_LOGW(TAG, "Failed to lock g_conn_cfg mutex");
                }
            }
        } else if (recv_len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {   // ignore timeout
            ESP_LOGE(TAG, "recvfrom failed: errno=%d, strerror=\"%s\"", errno, strerror(errno));
        }
    }

    ESP_LOGI(TAG, "Handshake task stopped");
    s_handshake_hndl = NULL;
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

        if (discovery_server_init() != 0) {
            ESP_LOGE(TAG, "Failed to init discovery server");
            xEventGroupSetBits(g_event_mgr.events, EV_DISCOVERY_INIT_FAILED);
            continue;
        }
        if (handshake_server_init() != 0) {
            ESP_LOGE(TAG, "Failed to init handshake server");
            discovery_server_destroy();
            xEventGroupSetBits(g_event_mgr.events, EV_DISCOVERY_INIT_FAILED);
            continue;
        }

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
        vTaskDelay(pdMS_TO_TICKS(DELAY_BEFORE_FORCE_TASK_DEL_MS));
        if (s_handshake_hndl != NULL) {
            vTaskDelete(s_handshake_hndl);
            s_handshake_hndl = NULL;
            ESP_LOGW(TAG, "Handshake task did not stop properly, forcing stop");
        }
        if (s_discovery_hndl != NULL) {
            vTaskDelete(s_discovery_hndl);
            s_discovery_hndl = NULL;
            ESP_LOGW(TAG, "Discovery task did not stop properly, forcing stop");
        }

        discovery_server_destroy();
        handshake_server_destroy();
    }
}
