#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/i2s_std.h"
#include <inttypes.h>

#include "discovery_server.h"
#include "rtp_server.h"
#include "wifi.h"

static const char *TAG = "WHP " __FILE__;

static i2s_chan_handle_t s_tx_chan;

#define RINGBUFFER_SIZE (64 * 1024)

RingbufHandle_t rb = NULL;
rtp_server_t rtp = { 0 };

static void audio_play(void *arg) {
    RingbufHandle_t rb = *(RingbufHandle_t*)arg;
    size_t item_size;
    uint8_t* item;

    while (1) {
        item = (uint8_t*)xRingbufferReceive(rb, &item_size, pdMS_TO_TICKS(100));

        if (item != NULL) {
            size_t bytes_read;
            i2s_channel_write(s_tx_chan, item, item_size, &bytes_read, pdMS_TO_TICKS(10));
            vRingbufferReturnItem(rb, item);
        }
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

static void i2s_init_std_simplex(void) {
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, &s_tx_chan, NULL));

    i2s_std_config_t tx_std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(CONFIG_AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_I2S_BCLK_GPIO,
            .ws = CONFIG_I2S_WS_GPIO,
            .dout = CONFIG_I2S_DIN_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx_chan, &tx_std_cfg));

    ESP_ERROR_CHECK(i2s_channel_enable(s_tx_chan));

    ESP_LOGI(TAG, "Initialized: 16kHz, stereo, 16-bit");
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
    i2s_init_std_simplex();
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
