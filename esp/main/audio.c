#include "audio.h"

#include "driver/i2s_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include <stdatomic.h>
#include <stdbool.h>

static const char *TAG = "WHP " __FILE__;
static TaskHandle_t s_audio_hndl = NULL;
static RingbufHandle_t s_rb_hndl = NULL;
static i2s_chan_handle_t s_i2s_hndl = NULL;
static atomic_bool s_running = ATOMIC_VAR_INIT(false);

#define I2S_WRITE_TIMEOUT_MS 10
#define RB_RECV_TIMEOUT_MS 100
#define DELAY_BEFORE_FORCE_TASK_DEL_MS (RB_RECV_TIMEOUT_MS + 100)

static int init_ringbuf(void) {
    s_rb_hndl = xRingbufferCreate(RINGBUFFER_SIZE, RINGBUF_TYPE_BYTEBUF);
    if (s_rb_hndl == NULL) {
        ESP_LOGE(TAG, "Failed to create ringbuf");
        return -1;
    }
    ESP_LOGI(TAG, "Ringbuf created successfully");
    return 0;
}

static void clear_ringbuf(void) {
    size_t item_size;
    char *item;

    while ((item = (char *)xRingbufferReceive(s_rb_hndl, &item_size, 0))) {
        vRingbufferReturnItem(s_rb_hndl, item);
    }

    ESP_LOGI(TAG, "Ringbuf cleared");
}

static void i2s_init_std_simplex(void) {
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, &s_i2s_hndl, NULL));

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
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_i2s_hndl, &tx_std_cfg));

    ESP_ERROR_CHECK(i2s_channel_enable(s_i2s_hndl));

    ESP_LOGI(TAG, "Initialized: sample_rate=%d, channels=%d, sample_size=%d",
             AUDIO_SAMPLE_RATE, AUDIO_CHANNELS, AUDIO_SAMPLE_SIZE * 8);
}

static void audio_play_task(void *arg) {
    size_t item_size;
    uint8_t* item;

    ESP_LOGI(TAG, "audio_play_task started");

    while (atomic_load(&s_running)) {
        item = (uint8_t*)xRingbufferReceive(s_rb_hndl, &item_size,
                                            pdMS_TO_TICKS(RB_RECV_TIMEOUT_MS));

        if (item != NULL) {
            size_t bytes_read;
            i2s_channel_write(s_i2s_hndl, item, item_size, &bytes_read,
                              pdMS_TO_TICKS(I2S_WRITE_TIMEOUT_MS));
            vRingbufferReturnItem(s_rb_hndl, item);
        }
    }

    ESP_LOGI(TAG, "audio_play_task stopped");
    s_audio_hndl = NULL;
    vTaskDelete(NULL);
}

int audio_init(void) {
    if (init_ringbuf() != 0) {
        ESP_LOGE(TAG, "Failed to create ringbuf");
        return -1;
    }
    i2s_init_std_simplex();

    return 0;
}

void audio_task_start(void) {
    atomic_store(&s_running, true);
    xTaskCreate(audio_play_task, "audio_play_task", 4096, NULL, 5, &s_audio_hndl);
}

void audio_task_stop(void) {
    atomic_store(&s_running, false);
    vTaskDelay(pdMS_TO_TICKS(DELAY_BEFORE_FORCE_TASK_DEL_MS));
    if (s_audio_hndl != NULL) {
        vTaskDelete(s_audio_hndl);
        s_audio_hndl = NULL;
        ESP_LOGW(TAG, "Audio task did not stop properly, forcing stop");
    }
    clear_ringbuf();
}

void audio_deinit_before_restart(void) {
    if (atomic_load(&s_running)) {
        audio_task_stop();
    } else {
        // if the audio task has not yet ended
        vTaskDelay(pdMS_TO_TICKS(DELAY_BEFORE_FORCE_TASK_DEL_MS));
    }

    if (s_audio_hndl != NULL) {
        vTaskSuspend(s_audio_hndl);
    }
    if (s_rb_hndl != NULL) {
        vRingbufferDelete(s_rb_hndl);
        s_rb_hndl = NULL;
    }
    if (s_i2s_hndl != NULL) {
        i2s_channel_disable(s_i2s_hndl);
        i2s_del_channel(s_i2s_hndl);
        s_i2s_hndl = NULL;
    }
    ESP_LOGI(TAG, "Audio deinitialized");
}

const RingbufHandle_t *get_rb_ptr(void) {
    return &s_rb_hndl;
}
