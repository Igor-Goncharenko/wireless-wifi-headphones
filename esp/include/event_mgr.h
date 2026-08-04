#ifndef EVENT_MGR_H
#define EVENT_MGR_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include <stdint.h>

typedef enum {
    ST_INITIALIZED      = BIT0,
    ST_DISCOVERY_ACTIVE = BIT1,
    ST_CLIENT_CONNECTED = BIT2,
    ST_FAILED           = BIT3,
} system_state_t;

typedef enum {
    EV_WIFI_GOT_IP              = BIT0,
    EV_WIFI_INIT_FAILED         = BIT1,
    EV_WIFI_DISCONNECTED        = BIT2,

    EV_DISCOVERY_FAILED         = BIT3,

    EV_CLIENT_CONNECTED         = BIT4,
    EV_CLIENT_DISCONNECTED      = BIT5,
    EV_CLIENT_LOST_CONNECTION   = BIT6,

    EV_RTP_INIT_FAILED          = BIT7,
    EV_CMDS_SERVER_INIT_FAILED  = BIT8,
} system_event_t;

typedef enum {
    ACT_START_DISCOVERY = BIT0,
    ACT_STOP_DISCOVERY  = BIT1,

    ACT_START_CL_CONN   = BIT2,
    ACT_STOP_CL_CONN    = BIT3,

    ACT_RESTART_SYSTEM  = BIT4,
    ACT_RECONNECT_WIFI  = BIT5,
    ACT_NOP             = 0,
} system_action_t;

#define EVENT_MGR_QUEUE_LEN 32

typedef struct {
    system_state_t state;
    QueueHandle_t event_queue;
    QueueHandle_t action_queue;
    SemaphoreHandle_t mutex;
} event_mgr_t;

typedef struct {
    system_state_t from;
    system_event_t event;
    system_state_t to;
    uint32_t actions;
} transition_t;

typedef struct {
    system_action_t action;
    void (*func)(void);
} action_t;

int event_mgr_start(void);

void event_mgr_stop(void);

BaseType_t event_mgr_send_event(system_event_t event);

system_state_t event_mgr_state(void);

#endif /* EVENT_MGR_H */
