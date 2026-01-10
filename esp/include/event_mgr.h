#ifndef EVENT_MGR_H
#define EVENT_MGR_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"

typedef enum {
    EVENT_WIFI_CONNECTED =  BIT0,
    EVENT_WIFI_FAILED = BIT1,
    EVENT_DISCOVERY_START = BIT2,
    EVENT_CLIENT_CONNECTED = BIT3,
    EVENT_RTP_UP = BIT4,
    EVENT_AUDIO_UP = BIT5,
} system_events_e;

typedef struct {
    SemaphoreHandle_t mutex;
    struct in_addr host_ip;
} connection_config_t;

extern EventGroupHandle_t g_system_events;
extern connection_config_t g_conn_cfg;

void event_mgr_task(void *arg);

#endif /* EVENT_MGR_H */
