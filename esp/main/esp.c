#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_err.h"

#define DYNAMIC_PIN GPIO_NUM_2
#define COUNT_NOTES 39

const int tones[COUNT_NOTES] = {
    392, 392, 392, 311, 466, 392, 311, 466, 392,
    587, 587, 587, 622, 466, 369, 311, 466, 392,
    784, 392, 392, 784, 739, 698, 659, 622, 659,
    415, 554, 523, 493, 466, 440, 466,
    311, 369, 311, 466, 392
};

const int durations[COUNT_NOTES] = {
    350, 350, 350, 250, 100, 350, 250, 100, 700,
    350, 350, 350, 250, 100, 350, 250, 100, 700,
    350, 250, 100, 350, 250, 100, 100, 100, 450,
    150, 350, 250, 100, 100, 100, 450,
    150, 350, 250, 100, 750
};

const ledc_timer_config_t ledc_timer = {
    .speed_mode       = LEDC_LOW_SPEED_MODE,
    .duty_resolution  = LEDC_TIMER_8_BIT,
    .timer_num        = LEDC_TIMER_0,
    .freq_hz          = 2000,
    .clk_cfg          = LEDC_AUTO_CLK
};

const ledc_channel_config_t ledc_channel = {
    .speed_mode     = LEDC_LOW_SPEED_MODE,
    .channel        = LEDC_CHANNEL_0,
    .timer_sel      = LEDC_TIMER_0,
    .intr_type      = LEDC_INTR_DISABLE,
    .gpio_num       = DYNAMIC_PIN,
    .duty           = 0,
    .hpoint         = 0
};

void app_main(void) {
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
    
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 128));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));

    while (1) {
        for (int i = 0; i < COUNT_NOTES; i++) {
            if (tones[i] > 0) {
                ESP_ERROR_CHECK(ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, tones[i]));
                
                ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 128));
                ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
                
                vTaskDelay(pdMS_TO_TICKS(durations[i]));
                
                ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0));
                ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
            }
            
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
