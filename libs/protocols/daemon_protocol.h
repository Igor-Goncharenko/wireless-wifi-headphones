#ifndef DAEMON_PROTOCOL_H
#define DAEMON_PROTOCOL_H

#define DEFAULT_DISCOVERY_DURATION 5

#define DAEMON_CMD_TYPE_LAST DAEMON_CMD_DISCONNECT
typedef enum {
    DAEMON_CMD_UNKNOWN = -1,
    DAEMON_CMD_STATUS = 0,
    DAEMON_CMD_DISCOVERY,
    DAEMON_CMD_DISCOVERY_DATA,
    DAEMON_CMD_CONNECT,
    DAEMON_CMD_DISCONNECT,
} daemon_cmd_type_e;

typedef struct {
    int duration;
} daemon_cmd_discovery_t;

typedef struct {
    char ip4[16];
} daemon_cmd_connect_t;

typedef struct {
    daemon_cmd_type_e type;
    union {
        daemon_cmd_discovery_t discovery;
        daemon_cmd_connect_t connect;
    };
} daemon_cmd_t;

#endif /* DAEMON_PROTOCOL_H */
