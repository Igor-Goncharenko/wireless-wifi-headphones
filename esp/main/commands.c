#include "commands.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "lwip/sockets.h"
#include "esp_log.h"
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "protocols/headphones.h"
#include "event_mgr.h"

#define COMMANDS_SOCK_TIMEOUT_MS 1000
#define DELAY_BEFORE_FORCE_TASK_DEL_MS (COMMANDS_SOCK_TIMEOUT_MS + 200)

#define CMDS_QUEUE_SIZE 16

static const char *TAG = "WHP " __FILE__;
static commands_server_t s_server = { 0 };
static bool s_running = false;
static TaskHandle_t s_cmds_recv_hndl = NULL;
static TaskHandle_t s_cmds_send_hndl = NULL;
static QueueHandle_t s_cmds_queue_hndl = NULL;

static int commands_server_init(void) {
    memset(&s_server, 0, sizeof(commands_server_t));

    if ((s_server.sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        ESP_LOGE(TAG, "Commands server failed to create socket: %s", strerror(errno));
        return -1;
    }

    int enable = 1;
    if (setsockopt(s_server.sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        ESP_LOGW(TAG, "setsockopt SO_REUSEADDR failed: %s", strerror(errno));
    }

    memset(&s_server.addr, 0, sizeof(s_server.addr));
    s_server.addr.sin_family = AF_INET;
    s_server.addr.sin_addr.s_addr = htonl(INADDR_ANY);
    s_server.addr.sin_port = htons(HEADPHONES_CMD_PORT);
    if (bind(s_server.sockfd, (struct sockaddr *)&s_server.addr, sizeof(s_server.addr)) < 0) {
        ESP_LOGE(TAG, "Commands server bind failed: %s", strerror(errno));
        close(s_server.sockfd);
        s_server.sockfd = -1;
        return -1;
    }

    struct timeval tv = {
        .tv_sec = COMMANDS_SOCK_TIMEOUT_MS / 1000,
        .tv_usec = COMMANDS_SOCK_TIMEOUT_MS % 1000,
    };
    if (setsockopt(s_server.sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        ESP_LOGW(TAG, "setsockopt SO_RCVTIMEO failed: %s", strerror(errno));
    }

    s_server.expected_sequence = 0;
    s_server.packets_received = 0;
    s_server.packets_lost = 0;

    s_server.allowed_ip4.s_addr = ipaddr_addr(g_event_mgr.host_ip4);

    ESP_LOGI(TAG, "Commands server initialized: sockfd=%d, port=%d", s_server.sockfd,
             HEADPHONES_CMD_PORT);
    return 0;
}

static void commands_server_destroy() {
    if (s_server.sockfd > 0) {
        shutdown(s_server.sockfd, SHUT_RDWR);
        vTaskDelay(pdMS_TO_TICKS(100));
        close(s_server.sockfd);
    }

    memset(&s_server, 0, sizeof(commands_server_t));
    ESP_LOGI(TAG, "Commands server destroyed");
}

static void process_command(headphones_packet_t command) {
    switch (command.command) {
        case HPCMD_NO_COMMAND:
            break;
        case HPCMD_PING:
            break;
        case HPCMD_DISCONNECT:
            xEventGroupSetBits(g_event_mgr.events, EV_CLIENT_DISCONNECTED);
            break;
        default:
            ESP_LOGW(TAG, "Unprocessed headphones command 0x%02x", command.command);
            break;
    }
}

static void commands_receiver_task(void *arg) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    headphones_packet_t packet;
    ssize_t recv_len;

    ESP_LOGI(TAG, "Commands receiver task started");

    while (s_running) {
        recv_len = recvfrom(s_server.sockfd, &packet, sizeof(headphones_packet_t), 0,
                            (struct sockaddr *)&client_addr, &client_len);

        if (recv_len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;  // ignore timeout
            ESP_LOGE(TAG, "recvfrom failed: %s", strerror(errno));

            int error = 0;
            socklen_t len = sizeof(error);
            if (getsockopt(s_server.sockfd, SOL_SOCKET, SO_ERROR, &error, &len) == 0) {
                ESP_LOGE(TAG, "Socket error: %d (%s)", error, strerror(error));
            }

            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (client_addr.sin_addr.s_addr != s_server.allowed_ip4.s_addr) {
            ESP_LOGW(TAG, "Rejected packet from: %s:%d, size: %d", inet_ntoa(client_addr.sin_addr),
                     ntohs(client_addr.sin_port), recv_len);
            continue;
        }

        if (recv_len != sizeof(headphones_packet_t)) {
            ESP_LOGW(TAG, "Incorrect packet size: %zd (expected %zu)", recv_len,
                     sizeof(headphones_packet_t));
            continue;
        }

        headphones_packet_t command = {
            .command = packet.command,
            .sequence = ntohs(packet.sequence),
            .timestamp = ntohl(packet.timestamp),
        };

        ESP_LOGI(TAG, "Received command: 0x%02x", command.command);
        process_command(command);
    }

    ESP_LOGI(TAG, "Commands receiver task stopped");
    s_cmds_recv_hndl = NULL;
    vTaskDelete(NULL);
}

static void commands_sender_task(void *arg) {
    headphones_packet_t cmd;
    ssize_t bytes_sent;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    client_addr.sin_family = AF_INET;
    memcpy(&client_addr.sin_addr, &s_server.allowed_ip4, sizeof(struct in_addr));
    client_addr.sin_port = htons(HEADPHONES_CMD_PORT);

    ESP_LOGI(TAG, "Commands sender task started");

    while (s_running) {
        if (xQueueReceive(s_cmds_queue_hndl, &cmd, pdMS_TO_TICKS(1000))) {
            bytes_sent = sendto(s_server.sockfd, &cmd, sizeof(headphones_packet_t), 0,
                                (struct sockaddr *)&client_addr, client_len);
            if (bytes_sent < 0) {
                ESP_LOGE(TAG, "Failed to send %02x command: %s", cmd.command, strerror(errno));
            } else {
                ESP_LOGI(TAG, "Send command: %02x", cmd.command);
            }
        }
    }

    ESP_LOGI(TAG, "Commands sender task stopped");
    s_cmds_recv_hndl = NULL;
    vTaskDelete(NULL);
}

static void commands_server_start(void) {
    if (commands_server_init() != 0) {
        ESP_LOGE(TAG, "Failed to init commands server");
        xEventGroupSetBits(g_event_mgr.events, EV_CMDS_SERVER_INIT_FAILED);
        return;
    }
    s_running = true;
    xTaskCreate(commands_receiver_task, "commands_receiver_task", 4096, NULL, 5, &s_cmds_recv_hndl);
    xTaskCreate(commands_sender_task, "commands_sender_task", 4096, NULL, 5, &s_cmds_recv_hndl);
}

static void commands_server_stop(void) {
    s_running = false;
    vTaskDelay(pdMS_TO_TICKS(DELAY_BEFORE_FORCE_TASK_DEL_MS));
    if (s_cmds_recv_hndl != NULL) {
        vTaskDelete(s_cmds_recv_hndl);
        s_cmds_recv_hndl = NULL;
        ESP_LOGW(TAG, "Command receiver task did not stop properly, forcing stop");
    }
    if (s_cmds_send_hndl != NULL) {
        vTaskDelete(s_cmds_send_hndl);
        s_cmds_send_hndl = NULL;
        ESP_LOGW(TAG, "Command sender task did not stop properly, forcing stop");
    }
    commands_server_destroy();
}

int push_command(uint8_t command_type) {
    headphones_packet_t new_command = {
        .command = command_type,
        .timestamp = (uint32_t)time(NULL),
        .sequence = (s_server.sender_seq++),
    };
    if (xQueueSend(s_cmds_queue_hndl, &new_command, pdMS_TO_TICKS(10))) {
        return 0;
    } else {
        ESP_LOGW(TAG, "Failed to send command to queue");
        return -1;
    }
}

void commands_server_mgr_task(void *arg) {
    s_cmds_queue_hndl = xQueueCreate(CMDS_QUEUE_SIZE, sizeof(headphones_packet_t));
    if (s_cmds_queue_hndl == NULL) {
        ESP_LOGE(TAG, "Failed to init commands server");
        xEventGroupSetBits(g_event_mgr.events, EV_CMDS_SERVER_INIT_FAILED);
        return;
    }

    while (1) {
        xEventGroupWaitBits(
            g_event_mgr.signals,
            SIG_START_COMMANDS,
            pdTRUE,
            pdTRUE,
            portMAX_DELAY
        );

        commands_server_start();

        xEventGroupWaitBits(
            g_event_mgr.signals,
            SIG_STOP_COMMANDS,
            pdTRUE,
            pdTRUE,
            portMAX_DELAY
        );

        commands_server_stop();
    }
}

void clear_commands_sock_before_restart(void) {
    if (s_running) {
        commands_server_stop();
    } else {
        // if the commands_server_stop has not yet ended
        vTaskDelay(pdMS_TO_TICKS(DELAY_BEFORE_FORCE_TASK_DEL_MS));
    }
    ESP_LOGI(TAG, "Commands server cleaned");
}
