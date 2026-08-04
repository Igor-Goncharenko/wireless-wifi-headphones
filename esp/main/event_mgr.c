#include "event_mgr.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include <stdatomic.h>
#include <stdint.h>

#include "audio.h"
#include "commands.h"
#include "discovery_server.h"
#include "rtp_server.h"
#include "wifi.h"

static const char *TAG = "WHP" __FILE__;

static event_mgr_t s_event_mgr = {0};
static TaskHandle_t s_event_mgr_hndl = NULL;
static TaskHandle_t s_event_mgr_actions_hndl = NULL;
static atomic_bool s_running = ATOMIC_VAR_INIT(false);

static const transition_t TRANSITIONS[] = {
    {ST_INITIALIZED, EV_WIFI_GOT_IP,  ST_DISCOVERY_ACTIVE, ACT_START_DISCOVERY},
    {ST_INITIALIZED, EV_WIFI_DISCONNECTED, ST_INITIALIZED, ACT_RECONNECT_WIFI},
    {ST_INITIALIZED, EV_WIFI_INIT_FAILED, ST_FAILED, ACT_RESTART_SYSTEM},

    {ST_DISCOVERY_ACTIVE, EV_CLIENT_CONNECTED, ST_CLIENT_CONNECTED, ACT_STOP_DISCOVERY | ACT_START_CL_CONN},
    {ST_DISCOVERY_ACTIVE, EV_WIFI_DISCONNECTED, ST_INITIALIZED, ACT_RECONNECT_WIFI},
    {ST_DISCOVERY_ACTIVE, EV_DISCOVERY_FAILED, ST_FAILED, ACT_RESTART_SYSTEM},

    {ST_CLIENT_CONNECTED, EV_CLIENT_DISCONNECTED, ST_DISCOVERY_ACTIVE, ACT_STOP_CL_CONN | ACT_START_DISCOVERY},
    {ST_CLIENT_CONNECTED, EV_CLIENT_LOST_CONNECTION, ST_DISCOVERY_ACTIVE, ACT_STOP_CL_CONN | ACT_START_DISCOVERY},
    {ST_CLIENT_CONNECTED, EV_RTP_INIT_FAILED, ST_FAILED, ACT_RESTART_SYSTEM},
    {ST_CLIENT_CONNECTED, EV_CMDS_SERVER_INIT_FAILED, ST_FAILED, ACT_RESTART_SYSTEM},
    {ST_CLIENT_CONNECTED, EV_WIFI_DISCONNECTED, ST_INITIALIZED, ACT_RECONNECT_WIFI},
};
static const size_t N_TRANSITIONS = sizeof(TRANSITIONS) / sizeof(transition_t);

static void action_cl_conn_start(void) {
    commands_server_start();
    rtp_start();
    audio_task_start();
}

static void action_cl_conn_stop(void) {
    audio_task_stop();
    rtp_stop();
    commands_server_stop();
}

static void restart_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_restart();
}

static void action_safe_restart(void) {
    clear_rtp_sock_before_restart();
    vTaskDelay(pdMS_TO_TICKS(50));

    clear_discovery_before_restart();
    vTaskDelay(pdMS_TO_TICKS(50));

    clear_commands_sock_before_restart();
    vTaskDelay(pdMS_TO_TICKS(50));

    audio_deinit_before_restart();
    vTaskDelay(pdMS_TO_TICKS(50));

    shutdown_wifi_before_restart();
    vTaskDelay(pdMS_TO_TICKS(50));

    event_mgr_stop();
    vTaskDelay(pdMS_TO_TICKS(50));

    xTaskCreate(restart_task, "restart_task", 4096, NULL, 1, NULL);
}

static const action_t ACTIONS[] = {
    {ACT_START_DISCOVERY, discovery_start},
    {ACT_STOP_DISCOVERY, discovery_stop},
    {ACT_START_CL_CONN, action_cl_conn_start},
    {ACT_STOP_CL_CONN, action_cl_conn_stop},
    {ACT_RESTART_SYSTEM, action_safe_restart},
    {ACT_RECONNECT_WIFI, NULL}, /* NOTE: No action for now */
};
static const size_t N_ACTIONS = sizeof(ACTIONS) / sizeof(action_t);

static const char *system_state_str(system_state_t s) {
    switch (s) {
    case ST_INITIALIZED: return "ST_INITIALIZED";
    case ST_DISCOVERY_ACTIVE: return "ST_DISCOVERY_ACTIVE";
    case ST_CLIENT_CONNECTED: return "ST_CLIENT_CONNECTED";
    case ST_FAILED: return "ST_FAILED";
    default: return "ST_UNKNOWN";
    }
}

static const char *system_event_str(system_event_t e) {
    switch (e) {
    case EV_WIFI_GOT_IP: return "EV_WIFI_GOT_IP";
    case EV_WIFI_INIT_FAILED: return "EV_WIFI_INIT_FAILED";
    case EV_WIFI_DISCONNECTED: return "EV_WIFI_DISCONNECTED";
    case EV_DISCOVERY_FAILED: return "EV_DISCOVERY_FAILED";
    case EV_CLIENT_CONNECTED: return "EV_CLIENT_CONNECTED";
    case EV_CLIENT_DISCONNECTED: return "EV_CLIENT_DISCONNECTED";
    case EV_CLIENT_LOST_CONNECTION: return "EV_CLIENT_LOST_CONNECTION";
    case EV_RTP_INIT_FAILED: return "EV_RTP_INIT_FAILED";
    case EV_CMDS_SERVER_INIT_FAILED: return "EV_CMDS_SERVER_INIT_FAILED";
    default: return "EV_UNKNOWN";
    }
}

static const char *system_action_str(system_action_t a) {
    switch (a) {
    case ACT_START_DISCOVERY: return "ACT_START_DISCOVERY";
    case ACT_STOP_DISCOVERY: return "ACT_STOP_DISCOVERY";
    case ACT_START_CL_CONN: return "ACT_START_CL_CONN";
    case ACT_STOP_CL_CONN: return "ACT_STOP_CL_CONN";
    case ACT_RESTART_SYSTEM: return "ACT_RESTART_SYSTEM";
    case ACT_RECONNECT_WIFI: return "ACT_RECONNECT_WIFI";
    case ACT_NOP: return "ACT_NOP";
    default: return "ACT_UNKNOWN";
    }
}

static const transition_t *find_transition(system_state_t state, system_event_t event) {
    for (size_t i = 0; i < N_TRANSITIONS; i++) {
        if (TRANSITIONS[i].from == state && TRANSITIONS[i].event == event) {
            return &TRANSITIONS[i];
        }
    }
    return NULL;
}

static BaseType_t handle_actions(uint32_t actions) {
    BaseType_t ret = pdTRUE;
    for (size_t i = 0; i < N_ACTIONS; i++) {
        if ((actions & ACTIONS[i].action) > 0) {
            BaseType_t ret_local = xQueueSend(s_event_mgr.action_queue, &i,
                                              pdMS_TO_TICKS(10));
            if (ret_local != pdTRUE) {
                ESP_LOGE(TAG, "Failed to add action '%s' to queue",
                         system_action_str(ACTIONS[i].action));
                ret = pdFALSE;
            }
        }
    }
    return ret;
}

static void handle_event(system_event_t event) {
    ESP_LOGI(TAG, "Handling event='%s', state='%s'",
             system_event_str(event), system_state_str(s_event_mgr.state));

    if (s_event_mgr.state == ST_FAILED) {
        ESP_LOGW(TAG, "In 'ST_FAILED' state, ignoring event '%s'",
                 system_event_str(event));
        return;
    }

    const transition_t *t = find_transition(s_event_mgr.state, event);

    if (t != NULL) {
        s_event_mgr.state = t->to;
        handle_actions(t->actions);
    } else {
        ESP_LOGE(TAG, "Incorrect transition: state(%s) -> event(%s)",
                 system_state_str(s_event_mgr.state), system_event_str(event));
    }
}

static void event_mgr_destroy(void) {
    if (s_event_mgr.event_queue) {
        vQueueDelete(s_event_mgr.event_queue);
        s_event_mgr.event_queue = NULL;
    }
    if (s_event_mgr.action_queue) {
        vQueueDelete(s_event_mgr.action_queue);
        s_event_mgr.action_queue = NULL;
    }

    if (s_event_mgr.mutex) {
        vSemaphoreDelete(s_event_mgr.mutex);
        s_event_mgr.mutex = NULL;
    }

    ESP_LOGI(TAG, "Event Manager destroyed");
}

static int event_mgr_init(void) {
    memset(&s_event_mgr, 0, sizeof(event_mgr_t));

    s_event_mgr.event_queue = xQueueCreate(EVENT_MGR_QUEUE_LEN, sizeof(system_event_t));
    s_event_mgr.action_queue = xQueueCreate(EVENT_MGR_QUEUE_LEN, sizeof(size_t));
    s_event_mgr.mutex = xSemaphoreCreateMutex();

    if (s_event_mgr.event_queue == NULL || s_event_mgr.action_queue == NULL ||
        s_event_mgr.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to init event groups");
        event_mgr_destroy();
        return -1;
    }

    s_event_mgr.state = ST_INITIALIZED;
    ESP_LOGI(TAG, "Event Manager initialized");
    return 0;
}

static void event_mgr_task(void *arg) {
    system_event_t event;

    ESP_LOGI(TAG, "Event Manager task started");

    while (s_running) {
        if (xQueueReceive(s_event_mgr.event_queue, &event, pdMS_TO_TICKS(1000)) == pdTRUE) {
            handle_event(event);
        }
    }

    s_event_mgr_hndl = NULL;
    vTaskDelete(NULL);
}

static void event_mgr_actions_task(void *arg) {
    size_t idx;

    ESP_LOGI(TAG, "Event Manager Actions task started");

    while (s_running) {
        if (xQueueReceive(s_event_mgr.action_queue, &idx, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (ACTIONS[idx].func != NULL) ACTIONS[idx].func();
        }
    }

    s_event_mgr_actions_hndl = NULL;
    vTaskDelete(NULL);
}


int event_mgr_start(void) {
    int ret = 0;

    if ((ret = event_mgr_init()) != 0) {
        ESP_LOGE(TAG, "Event Manager initialization failed");
        return ret;
    }

    atomic_store(&s_running, true);
    xTaskCreate(event_mgr_task, "event_mgr_task", 4096, NULL, 5, &s_event_mgr_hndl);
    xTaskCreate(event_mgr_actions_task, "event_mgr_actions_task", 4096, NULL, 5,
                &s_event_mgr_actions_hndl);

    ESP_LOGI(TAG, "Event Manager started");

    return 0;
}

void event_mgr_stop(void) {
    atomic_store(&s_running, false);
    vTaskDelay(pdMS_TO_TICKS(100));

    xSemaphoreTake(s_event_mgr.mutex, portMAX_DELAY);

    if (s_event_mgr_hndl != NULL) {
        vTaskDelete(s_event_mgr_hndl);
        s_event_mgr_hndl = NULL;
        ESP_LOGW(TAG, "Event Manager task did not stop properly, forcing stop");
    }

    if (s_event_mgr_actions_hndl != NULL) {
        vTaskDelete(s_event_mgr_actions_hndl);
        s_event_mgr_actions_hndl = NULL;
        ESP_LOGW(TAG, "Event Manager Actions task did not stop properly, forcing stop");
    }

    xSemaphoreGive(s_event_mgr.mutex);

    event_mgr_destroy();

    ESP_LOGI(TAG, "Event Manager stopped");
}

BaseType_t event_mgr_send_event(system_event_t event) {
    if (!atomic_load(&s_running)) return pdFALSE;

    BaseType_t ret = pdFALSE;

    if (xSemaphoreTake(s_event_mgr.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        ret = xQueueSend(s_event_mgr.event_queue, &event, pdMS_TO_TICKS(100));
        xSemaphoreGive(s_event_mgr.mutex);
    }
    if (ret != pdTRUE) {
        ESP_LOGW(TAG, "Event queue full, dropping event 0x%X", event);
    }
    return ret;
}

system_state_t event_mgr_state(void) {
    return s_event_mgr.state;
}
