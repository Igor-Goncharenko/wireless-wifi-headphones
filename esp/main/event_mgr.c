#include "event_mgr.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "WHP" __FILE__;

event_mgr_t g_event_mgr;

int init_event_mgr(event_mgr_t *mgr) {
    memset(mgr, 0, sizeof(event_mgr_t));

    if ((mgr->mutex = xSemaphoreCreateMutex()) == NULL) {
        ESP_LOGE(TAG, "Failed to init mutex");
        return -1;
    }

    mgr->states = xEventGroupCreate();
    mgr->events = xEventGroupCreate();
    mgr->signals = xEventGroupCreate();

    if (mgr->states == NULL || mgr->events == NULL || mgr->signals == NULL) {
        ESP_LOGE(TAG, "Failed to init event groups");
        return -1;
    }

    mgr->curr_state = ST_INITIALIZED;
    xEventGroupSetBits(mgr->states, ST_INITIALIZED);

    return 0;
}

void destroy_event_mgr(event_mgr_t *mgr) {
    if (mgr->mutex) vSemaphoreDelete(mgr->mutex);
    if (mgr->states) vEventGroupDelete(mgr->states);
    if (mgr->events) vEventGroupDelete(mgr->events);
    if (mgr->signals) vEventGroupDelete(mgr->signals);

    memset(mgr, 0, sizeof(event_mgr_t));
    ESP_LOGI(TAG, "event_mgr_t destroyed");
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
            break;
        default:
            break;
    }

    xSemaphoreGive(mgr->mutex);
}

void event_mgr_task(void *arg) {
    if (init_event_mgr(&g_event_mgr) != 0) {
        ESP_LOGE(TAG, "Failed to init connection manager");
        return;
    }

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

    destroy_event_mgr(&g_event_mgr);
}
