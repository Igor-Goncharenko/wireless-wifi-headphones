#include "audio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "driver/i2s_std.h"
#include "esp_log.h"

static const char *TAG = "WHP " __FILE__;

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

// static void clear_ringbuf(const RingbufHandle_t rb) {
//     size_t item_size;
//     char *item;
//
//     while ((item = (char *)xRingbufferReceive(rb, &item_size, 0))) {
//         vRingbufferReturnItem(rb, item);
//     }
// }

static void i2s_init_std_simplex(i2s_chan_handle_t *tx_chan) {
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, tx_chan, NULL));

    i2s_std_config_t tx_std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_SAMPLE_SIZE, I2S_CHANNEL),
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
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(*tx_chan, &tx_std_cfg));

    ESP_ERROR_CHECK(i2s_channel_enable(*tx_chan));

    ESP_LOGI(TAG, "Initialized: sample_rate=%d, channels=%d, sample_size=%d",
             AUDIO_SAMPLE_RATE, AUDIO_CHANNELS, AUDIO_SAMPLE_SIZE * 8);
}

int audio_init(audio_t *audio) {
    if (init_ringbuf(&audio->rb) != 0) {
        ESP_LOGE(TAG, "Failed to create ringbuf");
        return -1;
    }
    i2s_init_std_simplex(&audio->i2s);

    return 0;
}

void audio_play(void *arg) {
    audio_t *ctx = (audio_t *)arg;
    size_t item_size;
    uint8_t* item;

    while (1) {
        item = (uint8_t*)xRingbufferReceive(ctx->rb, &item_size, pdMS_TO_TICKS(100));

        if (item != NULL) {
            size_t bytes_read;
            i2s_channel_write(ctx->i2s, item, item_size, &bytes_read, pdMS_TO_TICKS(10));
            vRingbufferReturnItem(ctx->rb, item);
        }
    }
}

