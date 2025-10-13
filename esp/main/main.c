#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"
#include <inttypes.h>

#include "discovery_server.h"
#include "wifi.h"

static const char *TAG = "WHP " __FILE__;

static RingbufHandle_t s_audio_ringbuf = NULL;

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
    server_addr.sin_port = htons(CONFIG_RTP_PORT);
    
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Bind failed");
        close(sockfd);
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "RTP server started on port %d", CONFIG_RTP_PORT);
    
    while (1) {
        recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                           (struct sockaddr *)&client_addr, &client_len);
        
        if (recv_len > (int)sizeof(rtp_header_t)) {
            rtp_header_t* header = (rtp_header_t*)buffer;
            
            if (header->ver != 2) {
                ESP_LOGW(TAG, "Invalid RTP version");
                continue;
            }
            
            if (header->payload_types != CONFIG_RTP_PAYLOAD_TYPE) {
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

    xTaskCreate(discovery_server_task, "discovery_server", 4096, NULL, 5, NULL);
    xTaskCreate(rtp_receiver_task, "rtp_receiver_task", 4096, NULL, 5, NULL);
    xTaskCreate(audio_play, "audio_play", 4096, NULL, 6, NULL);
}
