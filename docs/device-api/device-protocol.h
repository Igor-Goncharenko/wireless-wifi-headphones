#ifndef DEVICE_PROTOCOL_H
#define DEVICE_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROTOCOL_MAGIC 0xDEADBEEF

/**
 * @brief Universal header for all protocol packets (except RTP)
 *
 * This header MUST be the first field of every protocol structure.
 * It ensures extensibility and provides basic validation.
 */
typedef struct {
    uint32_t magic;                 /**< Must be PROTOCOL_MAGIC (0xDEADBEEF) */
    uint16_t version;               /**< Protocol version */
    uint16_t length;                /**< Total packet length in bytes (including this header) */
} __attribute__((packed)) protocol_header_t;

/* ============================================================================
 * DISCOVERY
 * ============================================================================ */

/**
 * @brief Discovery request - broadcast by daemon to find devices
 *
 * Sent as UDP broadcast. All fields after the header are reserved for future use.
 */
typedef struct {
    protocol_header_t header;       /**< length = sizeof(discovery_request_t) */
    uint8_t reserved[16];           /**< Reserved for future extensions (zero-filled) */
} __attribute__((packed)) discovery_request_t;

/**
 * @brief Discovery response - sent by device to advertise its capabilities
 */
typedef struct {
    protocol_header_t header;       /**< length = sizeof(discovery_response_t) */

    /* Device identification */
    char name[64];                  /**< Human-readable device name (null-terminated) */
    char mac[18];                   /**< MAC address "XX:XX:XX:XX:XX:XX" (null-terminated) */
    uint8_t ip[16];                 /**< IPv4 or IPv6 address (binary format) */
    bool is_ipv6;                   /**< true = IPv6, false = IPv4 */

    uint16_t handshake_port;        /**< TCP port for handshake phase */

    uint8_t reserved[19];           /**< Reserved for future extensions (zero-filled) */
} __attribute__((packed)) discovery_response_t;

/* ============================================================================
 * HANDSHAKE
 * ============================================================================ */

/**
 * @brief Handshake request - client negotiates session parameters
 */
typedef struct {
    protocol_header_t header;       /**< length = sizeof(handshake_request_t) */
    uint8_t reserved[16];           /**< Reserved for future extensions (zero-filled) */
} __attribute__((packed)) handshake_request_t;

/**
 * @brief Handshake response - device assigns ports and confirms session
 */
typedef struct {
    protocol_header_t header;       /**< length = sizeof(handshake_response_t) */

    /* Assigned ports */
    struct {
        uint16_t rtp_port;          /**< UDP port for RTP audio stream */
        uint16_t commands_port;     /**< TCP port for command exchange */
        uint8_t reserved[4];        /**< Padding */
    } network;

    /* Negotiated audio parameters */
    struct {
        uint16_t sample_rate;       /**< Audio sample rate in Hz (e.g. 44100, 48000) */
        uint8_t channels;           /**< Audio channel count (1, 2, 4) */
        uint8_t bit_width;          /**< Audio bit depth (16, 24, 32) */
        uint8_t reserved[4];        /**< Padding */
    } audio;

    uint8_t reserved[16];           /**< Reserved for future extensions (zero-filled) */
} __attribute__((packed)) handshake_response_t;

/* ============================================================================
 * COMMANDS
 * ============================================================================ */

/**
 * @brief Command types (request and response)
 */
typedef enum {
    /* System commands */
    CMD_NOP = 0x00,                 /**< No operation (test connectivity) */
    CMD_PING = 0x01,                /**< Ping request (expects CMD_OK) */
    CMD_DISCONNECT = 0x02,          /**< Gracefully terminate session */
    CMD_GET_STATUS = 0x03,          /**< Request full device status */
    CMD_RESTART = 0x04,             /**< Restart device */

    /* Audio control */
    CMD_VOLUME_SET = 0x20,          /**< Set volume level (0-100) */
    CMD_VOLUME_GET = 0x21,          /**< Get current volume */

    /* Device info */
    CMD_BATTERY_GET = 0x40,         /**< Get battery level (0-100) */
    CMD_FIRMWARE_VERSION = 0x41,    /**< Get firmware api version (uint16_t) */
} command_request_type_t;

/**
 * @brief Response codes
 */
typedef enum {
    CMD_OK = 0x00,                  /**< Command executed successfully */
    CMD_ERROR_UNKNOWN = 0x01,       /**< Unknown or unsupported command */
    CMD_INVALID = 0x02,             /**< Invalid parameters */
} command_response_type_t;

/**
 * @brief Generic command request
 */
typedef struct {
    protocol_header_t header;       /**< length = sizeof(command_request_t) */

    uint8_t command;                /**< command_request_type_t */
    uint32_t timestamp;             /**< Client timestamp (ms since epoch) */
    uint16_t sequence;              /**< Monotonic sequence number (per session) */

    union {
        uint8_t volume_level;       /**< for CMD_VOLUME_SET: 0-100 */

        uint8_t raw[64];            /**< Generic payload for future commands */
    };

    uint8_t reserved[17];           /**< Reserved for future extensions (zero-filled) */
} __attribute__((packed)) command_request_t;

/**
 * @brief Generic command response
 */
typedef struct {
    protocol_header_t header;       /**< length = sizeof(command_response_t) */

    uint8_t command;                /**< command_request_type_t (from request) */
    uint8_t response;               /**< command_response_type_t */
    uint32_t timestamp;             /**< Client timestamp (ms since epoch) */
    uint16_t sequence;              /**< Monotonic sequence number (per session) */

    union {
        uint8_t volume_level;       /**< CMD_VOLUME_SET/CMD_VOLUME_GET: 0-100*/
        uint8_t battery_level;      /**< CMD_BATTERY_LEVEL: 0-100 */
        uint16_t version;           /**< CMD_FIRMWARE_VERSION */

        uint8_t raw[64];            /**< Generic payload for future commands */
    } payload;

    uint8_t reserved[16];           /**< Reserved for future extensions (zero-filled) */
} __attribute__((packed)) command_response_t;

/* ============================================================================
 * RTP HEADER
 * ============================================================================ */

/**
 * @brief RTP header for audio streaming
 *
 * Complies with RFC 3550. Payload follows immediately after the header.
 */
typedef struct {
    /* byte 0 */
    uint8_t contributor_count : 4;  /**< CSRC count (should be 0) */
    uint8_t ver : 2;                /**< RTP version (must be 2) */
    uint8_t p : 1;                  /**< Padding flag (0 = no padding) */
    uint8_t x : 1;                  /**< Extension flag (0 = no extension) */

    /* byte 1 */
    uint8_t payload_types : 7;      /**< RTP payload type (96-127 for dynamic) */
    uint8_t m : 1;                  /**< Marker bit (last packet of frame) */

    /* bytes 2-3 */
    uint16_t sequence;              /**< Incrementing sequence number */

    /* bytes 4-7 */
    uint32_t timestamp;             /**< RTP timestamp */

    /* bytes 8-11 */
    uint32_t ssrc;                  /**< Synchronization source ID (unique per stream) */
} __attribute__((packed)) rtp_header_t;

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_PROTOCOL_H */
