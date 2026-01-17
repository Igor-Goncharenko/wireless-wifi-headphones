#include "wifi.h"

#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <stdio.h>

#include "event_mgr.h"

static const char *TAG = "WHP " __FILE__;

#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK

static int s_retry_num = 0;
static char s_ip4_str[IP4ADDR_STRLEN_MAX];
static esp_event_handler_instance_t s_instance_any_id = NULL;
static esp_event_handler_instance_t s_instance_got_ip = NULL;

static esp_netif_t *s_netif_hndl = NULL;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                               void *event_data) {
    if (event_base != WIFI_EVENT) {
        ESP_LOGE(TAG, "Incorrect event_base=%d", event_base);
        return;
    }

    switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WiFi STA started, connecting...");
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGW(TAG, "WiFi disconnected. Reason: %d", disc->reason);

            if (s_retry_num < CONFIG_WIFI_MAXIMUM_RETRY) {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGW(TAG, "retry to connect to the AP %d/%d", s_retry_num,
                         CONFIG_WIFI_MAXIMUM_RETRY);
            } else {
                xEventGroupSetBits(g_event_mgr.events, EV_WIFI_INIT_FAILED);
            }
            ESP_LOGI(TAG,"connect to the AP fail");
            }
            break;
        case WIFI_EVENT_STA_CONNECTED: {
            wifi_event_sta_connected_t *conn = (wifi_event_sta_connected_t *)event_data;
            ESP_LOGI(TAG, "Connected to AP: %s (channel: %d)", conn->ssid, conn->channel);
            }
            break;
        default:
            break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                             void *event_data) {
    if (event_base != IP_EVENT) {
        ESP_LOGE(TAG, "Incorrect event_base=%d", event_base);
        return;
    }

    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

    switch (event_id) {
        case IP_EVENT_STA_GOT_IP:
            sprintf(s_ip4_str, IPSTR, IP2STR(&event->ip_info.ip));
            ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
            s_retry_num = 0;
            xEventGroupSetBits(g_event_mgr.events, EV_WIFI_GOT_IP);
            break;
        default:
            break;
    }
}

void wifi_init_sta(void) {
    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_netif_hndl = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                    &wifi_event_handler, NULL, &s_instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                    &ip_event_handler, NULL, &s_instance_got_ip));

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

    ESP_LOGI(TAG, "wifi_init_sta finished");
}

const char* wifi_get_ip4_str(void) {
    if (strlen(s_ip4_str) > 0) {
        return s_ip4_str;
    }
    return NULL;
}

void shutdown_wifi_before_restart(void) {
    wifi_mode_t mode;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi did not initialize, error: %s", esp_err_to_name(err));
        return;
    }

    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_wifi_stop();

    if (s_instance_any_id != NULL) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_instance_any_id);
        s_instance_any_id = NULL;
    }
    if (s_instance_got_ip != NULL) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_instance_got_ip);
        s_instance_got_ip = NULL;
    }

    esp_wifi_deinit();

    esp_netif_destroy_default_wifi(s_netif_hndl);
    s_netif_hndl = NULL;

    esp_netif_deinit();

    ESP_LOGI(TAG, "esp wifi deinitialized");
}
