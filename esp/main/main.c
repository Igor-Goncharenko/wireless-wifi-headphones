#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/ringbuf.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/sockets.h"

#include "sdkconfig.h"

#include <inttypes.h>

#define UDP_PORT 1234
#define AUDIO_BUFFER_SIZE 1024

#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK

static const char *TAG = "wifi station";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;

#define MULTICAST_GROUP "224.1.1.1"
#define DISCOVERY_PORT 5000
#define DISCOVERY_REQUEST "DISCOVER_HEADPHONES_REQUEST"

#define RTP_PORT 5002
#define RTP_PAYLOAD_TYPE 96
#define AUDIO_SAMPLE_RATE 44100
#define CHANNELS 2
#define SAMPLE_SIZE 2
#define FRAMES_PER_PACKET 256
#define I2S_NUM I2S_NUM_0

static RingbufHandle_t s_audio_ringbuf = NULL;

typedef struct {
    char model[32];
    char device_id[32];
    char ip_addr[16];
} device_info_t;

#pragma pack(push, 1)
typedef struct {
    // first byte
    uint8_t contributor_count : 4;
    uint8_t ver : 2;
    uint8_t p : 1;
    uint8_t x : 1;

    // second byte
    uint8_t payload_types : 7;
    uint8_t m : 1;

    // other
    uint16_t sequence;
    uint32_t timestamp;
    uint32_t ssrc;
} rtp_header_t;
#pragma pack(pop)

static device_info_t device_info;
static esp_ip4_addr_t ip4;

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < CONFIG_WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "retry to connect to the AP %d/%d", s_retry_num, CONFIG_WIFI_MAXIMUM_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ip4 = event->ip_info.ip;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = CONFIG_WIFI_PW_ID,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

static void init_device_info(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    
    snprintf(device_info.device_id, sizeof(device_info.device_id),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    strlcpy(device_info.model, "WiFi Headphones v1.0", sizeof(device_info.model));
    sprintf(device_info.ip_addr, IPSTR, IP2STR(&ip4)); 
    
    ESP_LOGI(TAG, "Device ID: %s", device_info.device_id);
    ESP_LOGI(TAG, "Model: %s", device_info.model);
}

static int create_discovery_response(char *buffer, size_t buffer_size)
{
    return snprintf(buffer, buffer_size,
                   "{\"type\":\"HEADPHONES_RESPONSE\","
                   "\"model\":\"%s\","
                   "\"id\":\"%s\","
                   "\"ip\":\"%s\"}",
                   device_info.model, device_info.device_id, device_info.ip_addr);
}

static void discovery_server_task(void *pvParameters)
{
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[128];
    char response[256];
    int recv_len;
    
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
    
    while (1) {
        recv_len = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0,
                           (struct sockaddr *)&client_addr, &client_len);
        
        if (recv_len > 0) {
            buffer[recv_len] = '\0';
            //ESP_LOGI(TAG, "Received: %s from " IPSTR, buffer, 
            //         IP2STR(&client_addr.sin_addr.s_addr));
            
            if (strcmp(buffer, DISCOVERY_REQUEST) == 0) {
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
            ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
        }
        
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    
    close(sockfd);
    vTaskDelete(NULL);
}

static void rtp_receiver_task(void *args) {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    uint8_t buffer[1500];
    int recv_len;
    
    uint16_t expected_sequence = 0;
    uint32_t packets_received = 0;
    uint32_t packets_lost = 0;
    
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        vTaskDelete(NULL);
        return;
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(RTP_PORT);
    
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Bind failed");
        close(sockfd);
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "RTP server started on port %d", RTP_PORT);
    
    while (1) {
        recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                           (struct sockaddr *)&client_addr, &client_len);
        
        if (recv_len > (int)sizeof(rtp_header_t)) {
            rtp_header_t* header = (rtp_header_t*)buffer;
            
            if (header->ver != 2) {
                ESP_LOGW(TAG, "Invalid RTP version");
                continue;
            }
            
            if (header->payload_types != RTP_PAYLOAD_TYPE) {
                ESP_LOGW(TAG, "Unexpected payload type: %d", header->payload_types);
                continue;
            }
            
            uint16_t sequence = ntohs(header->sequence);
            
            // check packet loss
            if (expected_sequence != 0 && sequence != expected_sequence) {
                packets_lost += (sequence - expected_sequence);
                ESP_LOGI(TAG, "Packet loss detected: expected %d, got %d", 
                         expected_sequence, sequence);
            }
            expected_sequence = sequence + 1;
            
            size_t audio_data_size = recv_len - sizeof(rtp_header_t);
            
            UBaseType_t res = xRingbufferSend(s_audio_ringbuf, buffer + sizeof(rtp_header_t), 
                                              audio_data_size, pdMS_TO_TICKS(100));
            if (res != pdTRUE) {
                ESP_LOGW(TAG, "Ring buffer full, dropped %d bytes", audio_data_size);
            } else {
                ESP_LOGD(TAG, "Received %d bytes, buffer free: %d", 
                         audio_data_size, xRingbufferGetCurFreeSize(s_audio_ringbuf));
            }

            packets_received++;

            ESP_LOGI(TAG, "Audio data got : %u; packets received : %" PRIu32, audio_data_size);
            
            if (packets_received % 100 == 0) {
                ESP_LOGI(TAG, "Received %lu packets, lost: %lu", 
                         packets_received, packets_lost);
            }
        }
        
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
    
    close(sockfd);
    vTaskDelete(NULL);
}

static void audio_play(void *arg) {
    size_t item_size;
    uint8_t* item;
    //uint8_t audio_buffer[1024];

    while (1) {
        item = (uint8_t*)xRingbufferReceive(s_audio_ringbuf, &item_size, pdMS_TO_TICKS(100));

        if (item != NULL) {
            vRingbufferReturnItem(s_audio_ringbuf, item);
        }

        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
}

static int init_ringbuf(void) {
    s_audio_ringbuf = xRingbufferCreate(64 * 1024, RINGBUF_TYPE_BYTEBUF);
    if (s_audio_ringbuf == NULL) {
        ESP_LOGE(TAG, "Failed to create ringbuf");
        return -1;
    }

    ESP_LOGI(TAG, "Ringbuf created successfully");
    return 0;
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    init_ringbuf();

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    vTaskDelay(pdMS_TO_TICKS(5000));

    init_device_info();
    
    xTaskCreate(discovery_server_task, "discovery_server", 4096, NULL, 5, NULL);
    xTaskCreate(rtp_receiver_task, "rtp_receiver_task", 4096, NULL, 5, NULL);
    xTaskCreate(audio_play, "audio_play", 4096, NULL, 6, NULL);
}
