#include "event_mgr.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

EventGroupHandle_t g_system_events;

void event_mgr_task(void *arg) {
    g_system_events = xEventGroupCreate();

    EventBits_t bits;

    while (1) {
        bits = xEventGroupWaitBits(
            g_system_events,
            0xFF,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY
        );

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    vEventGroupDelete(g_system_events);
}

