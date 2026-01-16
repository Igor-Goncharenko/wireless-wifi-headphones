#include "event_mgr.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "WHP" __FILE__;

event_mgr_t g_event_mgr;

int init_event_mgr(void) {
    memset(&g_event_mgr, 0, sizeof(event_mgr_t));

    if ((g_event_mgr.mutex = xSemaphoreCreateMutex()) == NULL) {
        ESP_LOGE(TAG, "Failed to init mutex");
        return -1;
    }

    g_event_mgr.states = xEventGroupCreate();
    g_event_mgr.events = xEventGroupCreate();
    g_event_mgr.signals = xEventGroupCreate();

    if (g_event_mgr.states == NULL || g_event_mgr.events == NULL || g_event_mgr.signals == NULL) {
        ESP_LOGE(TAG, "Failed to init event groups");
        return -1;
    }

    g_event_mgr.curr_state = ST_INITIALIZED;
    xEventGroupSetBits(g_event_mgr.states, ST_INITIALIZED);
    ESP_LOGI(TAG, "g_event_mgr initialized");
    return 0;
}

void destroy_event_mgr(void) {
    if (g_event_mgr.mutex) vSemaphoreDelete(g_event_mgr.mutex);
    if (g_event_mgr.states) vEventGroupDelete(g_event_mgr.states);
    if (g_event_mgr.events) vEventGroupDelete(g_event_mgr.events);
    if (g_event_mgr.signals) vEventGroupDelete(g_event_mgr.signals);

    memset(&g_event_mgr, 0, sizeof(event_mgr_t));
    ESP_LOGI(TAG, "g_event_mgr destroyed");
}

static void handle_state_machine(event_mgr_t *mgr, EventBits_t events, EventBits_t states) {
    if (xSemaphoreTake(mgr->mutex, pdMS_TO_TICKS(50)) == pdFALSE) {
        ESP_LOGE(TAG, "Failed to take mgr mutex");
        return;
    }
    if (events & EV_WIFI_DISCONNECTED) {
        xEventGroupClearBits(mgr->states, ST_WIFI_CONNECTED);
        xEventGroupSetBits(mgr->signals, SIG_RECONNECT_WIFI);
        mgr->curr_state = ST_INITIALIZED;

        switch (mgr->curr_state) {
            case ST_CLIENT_CONNECTED:
                xEventGroupSetBits(mgr->signals, SIG_STOP_RTP | SIG_STOP_AUDIO);
                xEventGroupClearBits(mgr->states, ST_CLIENT_CONNECTED);
                break;
            case ST_DISCOVERY_ACTIVE:
                xEventGroupSetBits(mgr->signals, SIG_STOP_DISCOVERY);
                xEventGroupClearBits(mgr->states, ST_DISCOVERY_ACTIVE);
                break;
            default:
                break;
        }
        ESP_LOGE(TAG, "Failed to init WiFi");
        xSemaphoreGive(mgr->mutex);
        return;
    }

    switch (mgr->curr_state) {
        case ST_INITIALIZED:
            if (events & EV_WIFI_GOT_IP) {
                xEventGroupSetBits(mgr->signals, SIG_START_DISCOVERY);
                xEventGroupSetBits(mgr->states, ST_WIFI_CONNECTED | ST_DISCOVERY_ACTIVE);
                mgr->curr_state = ST_DISCOVERY_ACTIVE;
                ESP_LOGI(TAG, "WiFi connected, starting discovery");
            }
            break;
        case ST_DISCOVERY_ACTIVE:
            if (events & EV_CLIENT_CONNECTED) {
                xEventGroupSetBits(mgr->signals, SIG_START_RTP | SIG_START_AUDIO | SIG_STOP_DISCOVERY);
                xEventGroupSetBits(mgr->states, ST_CLIENT_CONNECTED);
                xEventGroupClearBits(mgr->states, ST_DISCOVERY_ACTIVE);
                mgr->curr_state = ST_CLIENT_CONNECTED;
                ESP_LOGI(TAG, "WiFi connected, starting discovery");
            }
            break;
        case ST_CLIENT_CONNECTED:
            if (events & (EV_CLIENT_LOST_CONNECTION | EV_CLIENT_DISCONNECTED)) {
                xEventGroupSetBits(mgr->signals, SIG_START_DISCOVERY | SIG_STOP_RTP | SIG_STOP_AUDIO);
                xEventGroupClearBits(mgr->states, ST_CLIENT_CONNECTED);
                xEventGroupSetBits(mgr->states, ST_DISCOVERY_ACTIVE);
                mgr->curr_state = ST_DISCOVERY_ACTIVE;
                ESP_LOGI(TAG, "Client lost connection (or disconnected)");
            }
            if (events & EV_RTP_INIT_FAILED) {
                xEventGroupClearBits(mgr->states, 0xFFFFFF);
                xEventGroupSetBits(mgr->states, ST_FAILED);
                mgr->curr_state = ST_FAILED;
                ESP_LOGE(TAG, "RTP init failed, aborting headphones");
                // TODO: should reboot somehow
            }
            break;
        default:
            break;
    }

    xSemaphoreGive(mgr->mutex);
}

void event_mgr_task(void *arg) {
    EventBits_t current_states, new_events;

    while (1) {
        new_events = xEventGroupWaitBits(
            g_event_mgr.events,
            0xFF,
            pdTRUE,
            pdFALSE,
            portMAX_DELAY
        );
        current_states = xEventGroupGetBits(g_event_mgr.states);

        handle_state_machine(&g_event_mgr, new_events, current_states);
    }

    destroy_event_mgr();
}
