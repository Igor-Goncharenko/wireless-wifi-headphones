#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "audio.h"
#include "discovery_server.h"
#include "rtp_server.h"
#include "wifi.h"
#include "event_mgr.h"

static const char *TAG = "WHP " __FILE__;

rtp_server_t rtp = { 0 };

void app_main(void) {
    esp_err_t ret;
    audio_t audio;

    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    xTaskCreate(event_mgr_task, "event_mgr_task", 4096, NULL, 5, NULL);
    xTaskCreate(discovery_server_mgr_task, "discovery_server", 4096, NULL, 5, NULL);

    wifi_init_sta();
    vTaskDelay(pdMS_TO_TICKS(500));

    if (audio_init(&audio) != 0) {
        ESP_LOGE(TAG, "Failed to init audio");
        return;
    }

    if (rtp_server_init(&rtp, audio.rb) != 0) {
        ESP_LOGE(TAG, "Failed to init rtp server");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    xEventGroupSetBits(g_system_events, EVENT_DISCOVERY_START);

    while (1) {
        EventBits_t bits = xEventGroupGetBits(g_system_events);
        if (bits & EVENT_WIFI_CONNECTED)
            ESP_LOGI(TAG, "EVENT_WIFI_CONNECTED");

        if (bits & EVENT_WIFI_FAILED)
            ESP_LOGI(TAG, "EVENT_WIFI_FAILED");

        if (bits & EVENT_DISCOVERY_START)
            ESP_LOGI(TAG, "EVENT_DISCOVERY_START");

        if (bits & EVENT_CLIENT_CONNECTED)
            ESP_LOGI(TAG, "EVENT_CLIENT_CONNECTED");

        if (bits & EVENT_RTP_UP)
            ESP_LOGI(TAG, "EVENT_RTP_UP");

        if (bits & EVENT_AUDIO_UP)
            ESP_LOGI(TAG, "EVENT_AUDIO_UP");

        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    //xTaskCreate(rtp_receiver_task, "rtp_receiver_task", 4096, &rtp, 5, NULL);
    //xTaskCreate(audio_play, "audio_play", 4096, &rb, 6, NULL);
}
