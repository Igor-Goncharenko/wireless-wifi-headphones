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

void app_main(void) {
    esp_err_t ret;
    audio_t audio = { 0 };
    rtp_server_t rtp = { 0 };

    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    xTaskCreate(event_mgr_task, "event_mgr_task", 4096, NULL, 5, NULL);
    xTaskCreate(discovery_server_mgr_task, "discovery_server", 4096, NULL, 5, NULL);

    wifi_init_sta();

    if (audio_init(&audio) != 0) {
        ESP_LOGE(TAG, "Failed to init audio, aborting");
        return;
    }

    if (rtp_server_init(&rtp, audio.rb) != 0) {
        ESP_LOGE(TAG, "Failed to init rtp server, aborting");
        return;
    }

    if (init_event_mgr(&g_event_mgr) != 0) {
        ESP_LOGE(TAG, "Failed to init event manager, aborting");
        destroy_event_mgr(&g_event_mgr);
        return;
    }

    xTaskCreate(event_mgr_task, "event_mgr_task", 4096, NULL, 5, NULL);
    xTaskCreate(discovery_server_mgr_task, "discovery_server_mgr_task", 4096, NULL, 5, NULL);
    xTaskCreate(rtp_server_mgr_task, "rtp_server_mgr_task", 4096, &rtp, 5, NULL);
    xTaskCreate(audio_play_mgr, "audio_play_mgr", 4096, &audio, 5, NULL);

    while (1) {
        system_events_e events = xEventGroupGetBits(g_event_mgr.states);
        if (events & ST_INITIALIZED) ESP_LOGI(TAG, "ST_INITIALIZED");
        if (events & ST_DISCOVERY_ACTIVE) ESP_LOGI(TAG, "ST_DISCOVERY_ACTIVE");
        if (events & ST_CLIENT_CONNECTED) ESP_LOGI(TAG, "ST_CLIENT_CONNECTED");
        if (events & ST_WIFI_CONNECTED) ESP_LOGI(TAG, "ST_WIFI_CONNECTED");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
