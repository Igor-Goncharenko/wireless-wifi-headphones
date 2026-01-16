#ifndef EVENT_MGR_H
#define EVENT_MGR_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"

typedef enum {
    ST_INITIALIZED = BIT0,
    ST_DISCOVERY_ACTIVE = BIT1,
    ST_CLIENT_CONNECTED = BIT2,

    ST_WIFI_CONNECTED = BIT3,
    ST_FAILED = BIT10,
} system_states_e;

typedef enum {
    // ST_INITIALIZED
    EV_WIFI_GOT_IP = BIT0,
    EV_WIFI_INIT_FAILED = BIT1,
    // ST_DISCOVERY_ACTIVE
    EV_CLIENT_CONNECTED = BIT2,
    // ST_CLIENT_CONNECTED
    EV_CLIENT_DISCONNECTED = BIT3,
    EV_CLIENT_LOST_CONNECTION = BIT4,
    // other
    EV_WIFI_DISCONNECTED = BIT5,
    EV_RTP_INIT_FAILED = BIT6,
} system_events_e;

typedef enum {
    SIG_RECONNECT_WIFI = BIT0,
    SIG_START_DISCOVERY = BIT1,
    SIG_STOP_DISCOVERY = BIT2,
    SIG_START_RTP = BIT3,
    SIG_STOP_RTP = BIT4,
    SIG_START_AUDIO = BIT5,
    SIG_STOP_AUDIO = BIT6,
} system_signals_e;

typedef struct {
    SemaphoreHandle_t mutex;

    char host_ip4[IP4ADDR_STRLEN_MAX];

    system_states_e curr_state;

    EventGroupHandle_t states;
    EventGroupHandle_t events;
    EventGroupHandle_t signals;
} event_mgr_t;

extern event_mgr_t g_event_mgr;

void event_mgr_task(void *arg);

int init_event_mgr(event_mgr_t *mgr);

void destroy_event_mgr(event_mgr_t *mgr);

#endif /* EVENT_MGR_H */
