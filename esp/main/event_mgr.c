#include "event_mgr.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "WHP" __FILE__;

event_mgr_t g_event_mgr;

int init_event_mgr(void) {
    memset(&g_event_mgr, 0, sizeof(event_mgr_t));

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
    if (g_event_mgr.states) vEventGroupDelete(g_event_mgr.states);
    if (g_event_mgr.events) vEventGroupDelete(g_event_mgr.events);
    if (g_event_mgr.signals) vEventGroupDelete(g_event_mgr.signals);

    memset(&g_event_mgr, 0, sizeof(event_mgr_t));
    ESP_LOGI(TAG, "g_event_mgr destroyed");
}

static void handle_state_machine(EventBits_t events, EventBits_t states) {
    if (events & EV_WIFI_DISCONNECTED) {
        xEventGroupClearBits(g_event_mgr.states, ST_WIFI_CONNECTED);
        xEventGroupSetBits(g_event_mgr.signals, SIG_RECONNECT_WIFI);
        g_event_mgr.curr_state = ST_INITIALIZED;

        switch (g_event_mgr.curr_state) {
            case ST_CLIENT_CONNECTED:
                xEventGroupSetBits(g_event_mgr.signals, SIG_STOP_RTP | SIG_STOP_AUDIO | SIG_STOP_COMMANDS);
                xEventGroupClearBits(g_event_mgr.states, ST_CLIENT_CONNECTED);
                break;
            case ST_DISCOVERY_ACTIVE:
                xEventGroupSetBits(g_event_mgr.signals, SIG_STOP_DISCOVERY);
                xEventGroupClearBits(g_event_mgr.states, ST_DISCOVERY_ACTIVE);
                break;
            default:
                break;
        }
        ESP_LOGE(TAG, "Failed to init WiFi");
        return;
    }

    switch (g_event_mgr.curr_state) {
        case ST_INITIALIZED:
            if (events & EV_WIFI_GOT_IP) {
                xEventGroupSetBits(g_event_mgr.signals, SIG_START_DISCOVERY);
                xEventGroupSetBits(g_event_mgr.states, ST_WIFI_CONNECTED | ST_DISCOVERY_ACTIVE);
                g_event_mgr.curr_state = ST_DISCOVERY_ACTIVE;
                ESP_LOGI(TAG, "WiFi connected, starting discovery");
            }
            break;
        case ST_DISCOVERY_ACTIVE:
            if (events & EV_CLIENT_CONNECTED) {
                xEventGroupSetBits(g_event_mgr.signals, SIG_START_RTP | SIG_START_AUDIO |
                                   SIG_START_COMMANDS | SIG_STOP_DISCOVERY);
                xEventGroupSetBits(g_event_mgr.states, ST_CLIENT_CONNECTED);
                xEventGroupClearBits(g_event_mgr.states, ST_DISCOVERY_ACTIVE);
                g_event_mgr.curr_state = ST_CLIENT_CONNECTED;
                ESP_LOGI(TAG, "WiFi connected, starting discovery");
            }
            if (events & EV_DISCOVERY_INIT_FAILED) {
                xEventGroupSetBits(g_event_mgr.states, ST_FAILED);
                g_event_mgr.curr_state = ST_FAILED;
                ESP_LOGE(TAG, "Discovery init failed, aborting headphones");
                xEventGroupSetBits(g_event_mgr.signals, SIG_RESTART);
            }
            break;
        case ST_CLIENT_CONNECTED:
            if (events & (EV_CLIENT_LOST_CONNECTION | EV_CLIENT_DISCONNECTED)) {
                xEventGroupSetBits(g_event_mgr.signals, SIG_START_DISCOVERY | SIG_STOP_RTP |
                                   SIG_STOP_AUDIO | SIG_STOP_COMMANDS);
                xEventGroupClearBits(g_event_mgr.states, ST_CLIENT_CONNECTED);
                xEventGroupSetBits(g_event_mgr.states, ST_DISCOVERY_ACTIVE);
                g_event_mgr.curr_state = ST_DISCOVERY_ACTIVE;
                ESP_LOGI(TAG, "Client lost connection (or disconnected)");
            }
            if (events & (EV_RTP_INIT_FAILED | EV_CMDS_SERVER_INIT_FAILED)) {
                xEventGroupSetBits(g_event_mgr.states, ST_FAILED);
                g_event_mgr.curr_state = ST_FAILED;
                ESP_LOGE(TAG, "RTP/Commands init failed, aborting headphones");
                xEventGroupSetBits(g_event_mgr.signals, SIG_RESTART);
            }
            break;
        default:
            break;
    }
}

void event_mgr_task(void *arg) {
    EventBits_t current_states, new_events;

    while (1) {
        new_events = xEventGroupWaitBits(
            g_event_mgr.events,
            ALL_USER_BITS,
            pdTRUE,
            pdFALSE,
            portMAX_DELAY
        );
        current_states = xEventGroupGetBits(g_event_mgr.states);

        handle_state_machine(new_events, current_states);
    }

    destroy_event_mgr();
}
