#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <inttypes.h>

#include "discovery_server.h"
#include "rtp_server.h"
#include "wifi.h"

static const char *TAG = "WHP " __FILE__;

#define RINGBUFFER_SIZE (64 * 1024)

RingbufHandle_t rb = NULL;
rtp_server_t rtp = { 0 };

static void audio_play(void *arg) {
    RingbufHandle_t rb = *(RingbufHandle_t*)arg;
    size_t item_size;
    uint8_t* item;
    //uint8_t audio_buffer[1024];

    while (1) {
        item = (uint8_t*)xRingbufferReceive(rb, &item_size, pdMS_TO_TICKS(100));

        if (item != NULL) {
            vRingbufferReturnItem(rb, item);
        }

        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
}

static int init_ringbuf(RingbufHandle_t *rb) {
    if (rb == NULL) {
        ESP_LOGE(TAG, "Ringbuf pointer is NULL");
        return -1;
    }

    *rb = xRingbufferCreate(RINGBUFFER_SIZE, RINGBUF_TYPE_BYTEBUF);
    if (*rb == NULL) {
        ESP_LOGE(TAG, "Failed to create ringbuf");
        return -1;
    }
    ESP_LOGI(TAG, "Ringbuf created successfully");
    return 0;
}

static void clear_ringbuf(const RingbufHandle_t rb) {
    size_t item_size;
    char *item;
    
    while ((item = (char *)xRingbufferReceive(rb, &item_size, 0))) {
        vRingbufferReturnItem(rb, item);
    }
}

void app_main(void) {
    esp_err_t ret;

    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init_sta();
    vTaskDelay(pdMS_TO_TICKS(500));

    if (init_ringbuf(&rb) != 0) {
        ESP_LOGE(TAG, "Failed to create ringbuf");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    if (rtp_server_init(&rtp, rb) != 0) {
        ESP_LOGE(TAG, "Failed to init rtp server");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    xTaskCreate(discovery_server_task, "discovery_server", 4096, NULL, 5, NULL);
    xTaskCreate(rtp_receiver_task, "rtp_receiver_task", 4096, &rtp, 5, NULL);
    xTaskCreate(audio_play, "audio_play", 4096, &rb, 6, NULL);
}
