#include "event_mgr.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

EventGroupHandle_t g_system_events;
connection_config_t g_conn_cfg;

void event_mgr_task(void *arg) {
    g_system_events = xEventGroupCreate();
    memset(&g_conn_cfg, 0, sizeof(connection_config_t));
    g_conn_cfg.mutex = xSemaphoreCreateMutex();

    while (1) {
        // EventBits_t bits = xEventGroupWaitBits(
        //     g_system_events,
        //     0xFF,
        //     pdFALSE,
        //     pdFALSE,
        //     portMAX_DELAY
        // );

        vTaskDelay(pdMS_TO_TICKS(10000));
    }

    vEventGroupDelete(g_system_events);
    vSemaphoreDelete(g_conn_cfg.mutex);
}

