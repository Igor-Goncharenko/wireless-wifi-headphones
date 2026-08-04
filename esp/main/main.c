#include "esp_log.h"
#include "nvs_flash.h"

#include "audio.h"
#include "event_mgr.h"
#include "state.h"
#include "wifi.h"

static const char *TAG = "WHP " __FILE__;

char host_ip4[IP4ADDR_STRLEN_MAX] = {0};

void app_main(void) {
    esp_err_t ret;

    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (event_mgr_start() != 0) {
        ESP_LOGE(TAG, "Failed to start Event Manager");
        return;
    }

    if (audio_init() != 0) {
        ESP_LOGE(TAG, "Failed to init audio, aborting");
        return;
    }

    wifi_init_sta();
}
