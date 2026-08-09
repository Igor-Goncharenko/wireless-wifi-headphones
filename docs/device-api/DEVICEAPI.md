# Device API Specification

**Version:** 0.0.0 (in development)


# 1. Overview
## 1.1 Purpose

This protocol defines the communication interface between a desktop daemon (client) and an edge
device (e.g., wireless headphones, smart speaker, audio receiver). The protocol enables:
1. Device discovery on the local network
2. Session negotiation and parameter configuration
3. Remote control and status monitoring
4. High-quality audio streaming

## 1.2 Scope

The protocol covers four distinct phases of communication:
1. Discovery — Locating available edge devices on the network
2. Handshake — Negotiating audio parameters and establishing communication channels
3. Command Exchange — Sending control commands and receiving responses
4. Audio Streaming — Transmitting real-time audio data via RTP

## 1.3 Conventions

**Data Types**

All protocol structures are defined using C-style data types from `<stdint.h>` and `<stdbool.h>`.
Implementations in other languages SHOULD use equivalent fixed-size types:

| C Type     | Size (bytes) | Description                                   |
| :--------- | :----------- | :-------------------------------------------- |
| `uint8_t`  | 1            | Unsigned 8-bit integer                        |
| `uint16_t` | 2            | Unsigned 16-bit integer (little-endian)       |
| `uint32_t` | 4            | Unsigned 32-bit integer (little-endian)       |
| `bool`     | 1            | Boolean value (0 = false, 1 = true)           |
| `char`     | N            | N	Fixed-size ASCII string (null-terminated)   |

**Byte Order**

All multi-byte fields (`uint16_t`, `uint32_t`) are transmitted in little-endian byte order.

**Structure Alignment**

All protocol structures are packed (`__attribute__((packed))` or equivalent) to ensure no padding
bytes are inserted between fields.

**Reserved Fields**

Fields marked as reserved MUST be set to zero by the sender and SHOULD be ignored by the receiver.
These fields are reserved for future protocol extensions.

**Magic Number**

All non-RTP packets MUST begin with the magic number `0xDEADBEEF` to validate packet integrity and
identify protocol packets.

# 2. Common Header

## 2.1. Constants (Macros)

| Constant (Macro) | Value        | Description                                       |
| :--------------- | :----------- | :------------------------------------------------ |
| `PROTOCOL_MAGIC` | `0xDEADBEEF` | Needed to validate the correctness of the package |

## 2.2. Protocol header

All protocol packets (except RTP) share a common header structure that provides basic validation and
extensibility.

| Field     | Type       | Size | Description                                          |
| :-------- | :--------- | :--- | :--------------------------------------------------- |
| `magic`   | `uint32_t` | 4    | MUST be PROTOCOL_MAGIC (0xDEADBEEF)                  |
| `version` | `uint16_t` | 2    | Protocol version (e.g., 0x0100 for v1.0)             |
| `length`  | `uint16_t` | 2    | Total packet length in bytes (including this header) |

```c
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
```

## 2.3. Discovery phase

The Discovery phase allows a desktop daemon to locate available edge devices on the local network.
The daemon broadcasts a discovery request, and all compatible devices respond with their
identification and handshake port information.

**Transport:** UDP (broadcast/multicast)

**Port:** Configurable (typically in the range 60000-60010)

**Direction:** Daemon → Device (request), Device → Client (response)

The request SHOULD be sent as a UDP broadcast to the network broadcast address
(e.g., `192.168.0.255/24`) or to a configured multicast group. The daemon SHOULD listen on the
same port for responses.

### Discovery Request (`discovery_request_t`)

The desktop daemon broadcasts this packet to discover all available edge devices on the network.

| Field      | Type                | Size | Description                                   |
| :--------- | :------------------ | :--- | :-------------------------------------------- |
| `header`   | `protocol_header_t` | 8    | Protocol header (magic, version, length)      |
| `reserved` | `uint8_t[16]`       | 16   | Reserved for future use (MUST be zero-filled) |

**Total Size:** 24 bytes

```c
/**
 * @brief Discovery request - broadcast by daemon to find devices
 *
 * Sent as UDP broadcast. All fields after the header are reserved for future use.
 */
typedef struct {
    protocol_header_t header;       /**< length = sizeof(discovery_request_t) */
    uint8_t reserved[16];           /**< Reserved for future extensions (zero-filled) */
} __attribute__((packed)) discovery_request_t;
```

### Discovery Response (`discovery_response_t`)

An edge device sends this response to announce its presence and capabilities after receiving a valid
discovery request.

| Field            | Type                | Size | Description                                                 |
| :--------------- | :---                | :--- | :---------------------------------------------------------- |
| `header`         | `protocol_header_t` | 8    | Protocol header (magic, version, length)                    |
| `name`           | `char[64]`          | 64   | Human-readable device name (null-terminated ASCII)          |
| `mac`            | `char[18]`          | 18   | MAC address in format `XX:XX:XX:XX:XX:XX` (null-terminated) |
| `ip`             | `uint8_t[16]`       | 16   | IPv4 or IPv6 address in binary format (network byte order)  |
| `is_ipv6`        | `bool`              | 1    | `true` = IPv6 address, `false` = IPv4 address               |
| `handshake_port` | `uint16_t`          | 2    | TCP port for the handshake phase                            |
| `reserved`       | `uint8_t[19]`       | 19   | Reserved for future use (MUST be zero-filled)               |

**Total Size:** 128 bytes

**Response Behavior**

Upon receiving a discovery request, the device SHOULD:
1. Validate the request header (magic, length, version)
2. Prepare a response with its current configuration
3. Send the response to the source address and port of the request
4. NOT respond if the request is malformed or invalid

**IP Address Handling**
- IPv4 addresses are stored in bytes 0-3 of the `ip` field, with the remaining bytes set to zero
- IPv6 addresses use all 16 bytes of the `ip` field
- The `is_ipv6` flag determines how to interpret the ip field

```c
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
```


## 2.4. Handshake Phase

The Handshake phase establishes a session between the desktop daemon and the edge device. The daemon
connects via TCP, negotiates audio parameters, and receives the actual port assignments for command
exchange and RTP audio streaming.

**Transport:** TCP

**Port:** Received in `discovery_response_t::handshake_port`

**Direction:** Daemon → Device (request), Device → Client (response)

### Handshake Request (`handshake_request_t`)

The desktop daemon initiates the handshake to request a session with the edge device and negotiate
audio parameters.

| Field	   | Type                | Size | Description                                   |
| :------- | :------------------ | :--- | :-------------------------------------------- |
| header   | `protocol_header_t` | 8    | Protocol header (magic, version, length)      |
| reserved | `uint8_t[16]`       | 16   | Reserved for future use (MUST be zero-filled) |

**Total size:** 24 bytes

```c
/**
 * @brief Handshake request - client negotiates session parameters
 */
typedef struct {
    protocol_header_t header;       /**< length = sizeof(handshake_request_t) */
    uint8_t reserved[16];           /**< Reserved for future extensions (zero-filled) */
} __attribute__((packed)) handshake_request_t;
```

**Connection Behavior**

The daemon SHOULD:
1. Establish a TCP connection to device_ip:handshake_port
2. Send the handshake request immediately after connection
3. Wait for the handshake response
4. Close the connection after receiving the response (or handle error)

**Timeout Handling**
1. TCP connection timeout: 3 seconds
2. Response timeout: 5 seconds
3. On timeout: close connection and retry with exponential backoff

### Handshake Response (`handshake_response_t`)

The edge device responds with the negotiated audio parameters and port assignments for command and
RTP communication.

| Field                   | Type                | Size | Description                                         |
| :---------------------- | :------------------ | :--- | :-------------------------------------------------- |
| `header`                | `protocol_header_t` | 8    | Protocol header (magic, version, length)            |
| `network.rtp_port`      | `uint16_t`          | 2    | UDP port for receiving RTP audio stream             |
| `network.commands_port` | `uint16_t`          | 2    | TCP port for command exchange                       |
| `network.reserved`      | `uint8_t[4]`        | 4    | Reserved for future use (MUST be zero-filled)       |
| `audio.sample_rate`     | `uint16_t`          | 2    | Audio sample rate in Hz (e.g., 44100, 48000, 96000) |
| `audio.channels`        | `uint8_t`           | 1    | Number of audio channels (1=mono, 2=stereo, 4=quad) |
| `audio.bit_width`       | `uint8_t`           | 1    | Bits per sample (16, 24, or 32)                     |
| `audio.reserved`        | `uint8_t[4]`        | 4    | Reserved for future use (MUST be zero-filled)       |
| `reserved`              | `uint8_t[16]`       | 16   | Reserved for future use (MUST be zero-filled)       |

**Total Size:** 40 bytes

**Parameter Negotiation**

Currently, the protocol uses server-driven negotiation, meaning:
1. The device determines the audio parameters (sample rate, channels, bit width)
2. The client MUST use the parameters provided in the response
3. Future versions may add client-side negotiation via the reserved fields

**Post-Handshake Actions**

After receiving the handshake response, the client MUST:
1. Open UDP socket on rtp_port to receive audio
2. Establish TCP connection to commands_port for command exchange
3. Store audio parameters for decoding RTP payloads
4. Maintain both connections for the duration of the session

```c
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
```


## 2.5. Command Exchange

The Command Exchange phase enables the desktop daemon to control the edge device and query its status.
All commands are sent over a persistent TCP connection.

**Transport:** TCP

**Port:** Received in handshake_response_t::network.commands_port

**Direction:** Daemon → Device (requests), Device → Daemon (responses)

The command port is persistent — the client SHOULD keep the TCP connection open for the duration of
the session and reuse it for multiple commands.

**Sequence Numbers**

Each command request includes a monotonic sequence number:
1. The client increments the sequence number for each new command
2. The device echoes the sequence number in the response
3. This allows the client to match responses with requests
4. Sequence numbers start at 0 and wrap around at 65535

**Timestamps**

Each command includes a timestamp field:
1. The client sets this to the current time in milliseconds since Unix epoch
2. The device echoes the timestamp in the response
3. This helps correlate requests and responses
4. Useful for latency measurement and debugging

### Command Types (`command_request_type_t`)

| Command                | Value  | Description                            |
| :--------------------- | :----- | :------------------------------------- |
| System Commands                                                         ||
| `CMD_NOP`              | `0x00` | No operation (test connectivity)       |
| `CMD_PING`             | `0x01` | Ping request (expects CMD_OK response) |
| `CMD_DISCONNECT`       | `0x02` | Gracefully terminate the session       |
| `CMD_GET_STATUS`       | `0x03` | Request full device status             |
| `CMD_RESTART`          | `0x04` | Restart the device                     |
| Audio Control                                                           ||
| `CMD_VOLUME_SET`       | `0x20` | Set volume level (0-100)               |
| `CMD_VOLUME_GET`       | `0x21` | Get current volume level               |
| Device Info                                                             ||
| `CMD_BATTERY_GET`      | `0x40` | Get battery level (0-100%)             |
| `CMD_FIRMWARE_VERSION` | `0x41` | Get firmware API version (uint16_t)    |

```c
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
```

### Response Codes (`command_response_type_t`)

| Code                   | Value  | Description                            |
| :--------------------- | :----- | :------------------------------------- |
| `CMD_OK`               | `0x00` | Command executed successfully          |
| `CMD_ERROR_UNKNOWN`    | `0x01` | Unknown or unsupported command         |
| `CMD_INVALID`          | `0x02` | Invalid parameters                     |

```c
/**
 * @brief Response codes
 */
typedef enum {
    CMD_OK = 0x00,                  /**< Command executed successfully */
    CMD_ERROR_UNKNOWN = 0x01,       /**< Unknown or unsupported command */
    CMD_INVALID = 0x02,             /**< Invalid parameters */
} command_response_type_t;
```

### Command Request (`command_request_t`)

| Field          | Type                | Size | Description                                              |
| :------------- | :------------------ | :--- | :------------------------------------------------------- |
| `header`       | `protocol_header_t` | 8    | Protocol header (magic, version, length)                 |
| `command`      | `uint8_t`           | 1    | Command ID (see command_request_type_t)                  |
| `timestamp`    | `uint32_t`          | 4    | Client timestamp in milliseconds since epoch (Unix time) |
| `sequence`     | `uint16_t`          | 2    | Monotonic sequence number, incremented per command       |
| `payload`      | `union`             | 64   | Command-specific payload (see below)                     |
| `reserved`     | `uint8_t[17]`       | 17   | Reserved for future use (MUST be zero-filled)            |
| Payload Union                                                                                         ||
| `volume_level` | `uint8_t`           | 1    | CMD_VOLUME_SET (0-100)                                   |
| `raw`          | `uint8_t[64]`       | 64   | Future commands (use with caution)                       |

**Total size:** 96 bytes

### Command Response (`command_response_t`)

```c
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
```

| Field           | Type                | Size | Description                                   |
| :-------------- | :------------------ | :--- | :-------------------------------------------- |
| `header`        | `protocol_header_t` | 8    | Protocol header (magic, version, length)      |
| `command`       | `uint8_t`           | 1    | Echo of the request command                   |
| `response`      | `uint8_t`           | 1    | Response code (see command_response_type_t)   |
| `timestamp`     | `uint32_t`          | 4    | Echo of request timestamp                     |
| `sequence`      | `uint16_t`          | 2    | Echo of request sequence                      |
| `payload`       | `union`             | 64   | Response payload (see below)                  |
| `reserved`      | `uint8_t[16]`       | 16   | Reserved for future use (MUST be zero-filled) |
| Payload Union                                                                               ||
| `volume_level`  | `uint8_t`           | 1    | CMD_VOLUME_SET or CMD_VOLUME_GET (0-100)      |
| `battery_level` | `uint8_t`           | 1    | CMD_BATTERY_GET (0-100%)                      |
| `version`       | `uint16_t`          | 2    | CMD_FIRMWARE_VERSION (e.g., 0x0100 = v1.0)    |
| `raw`           | `uint8_t[64]`       | 64   | Future commands (use with caution)            |

**Total size:** 96 bytes

```c
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
```

## 2.6. Audio streaming

Audio is transmitted from the edge device to the desktop daemon using the Real-time Transport
Protocol (RTP) as defined in RFC 3550. RTP provides:
1. Sequencing — Detecting packet loss and reordering
2. Timing — Synchronizing audio playback
3. Payload identification — Supporting different audio codecs

**Transport:** UDP

**Port:** Received in handshake_response_t::network.rtp_port

**Direction:** Daemon → Device (one-way streaming)

| Field               | Type       | Bits | Offset | Description                                                                            |
| :------------------ | :--------- | :--- | :----- | :------------------------------------------------------------------------------------- |
| Byte 0                                                                                                                                   ||
| `contributor_count` | `uint8_t`  | 4    | 0      | CSRC count. MUST be 0 for this protocol.                                               |
| `ver`               | `uint8_t`  | 2    | 0      | RTP version. MUST be 2.                                                                |
| `p`                 | `uint8_t`  | 1    | 0      | Padding flag. 0 = no padding.                                                          |
| `x`                 | `uint8_t`  | 1    | 0      | Extension flag. 0 = no extension.                                                      |
| Byte 1                                                                                                                                   ||
| `payload_types`     | `uint8_t`  | 7    | 1      | RTP payload type. Use 96-127 for dynamic payload types.                                |
| `m`                 | `uint8_t`  | 1    | 1      | Marker bit. Set on the last packet of an audio frame.                                  |
| Bytes 2-3                                                                                                                                ||
| `sequence`          | `uint16_t` | 16   | 2      | Incrementing sequence number. Starts at a random value and increments by 1 per packet. |
| Bytes 4-7                                                                                                                                ||
| `timestamp`         | `uint32_t` | 32   | 4      | RTP timestamp. Increments based on sample rate.                                        |
| Bytes 8-11                                                                                                                               ||
| `ssrc`              | `uint32_t` | 32   | 8      | Synchronization source identifier. Unique per audio stream.                            |

```c
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
```

**Sequence Number Handling**

The client SHOULD:
1. Track the expected sequence number
2. Detect and handle out-of-order packets
3. Detect packet loss (missing sequence numbers)
4. Use the sequence number for jitter buffer management

**Timestamp Handling**

The RTP timestamp increments based on the sample rate: `timestamp_increment = (samples_per_frame)`

For example, with 48kHz sample rate and 10ms frames: `timestamp_increment = 48000 * 0.010 = 480 samples`

**SSRC (Synchronization Source)**

The SSRC:
1. Uniquely identifies the audio stream
2. Is randomly generated by the device at session start
3. Remains constant for the duration of the session
4. Allows multiple audio sources to be mixed

Since the device will support one audio source, this field can be ignored.

**Audio format specification**

The audio format is specified during the handshake phase:

| Parameter  | Description                                                    |
| :--------- | :------------------------------------------------------------- |
| Encoding   | Raw PCM (no compression)                                       |
| Sample     | Rate	As specified in `handshake_response_t::audio.sample_rate` |
| Channels   | As specified in `handshake_response_t::audio.channels`         |
| Bit Width  | As specified in `handshake_response_t::audio.bit_width`        |
| Endianness | Little-endian                                                  |

### RTP Packet Size Calculation

The total RTP packet size consists of the fixed header and the variable payload:
<<<<<<< HEAD
```
packet_size = sizeof(rtp_header_t) + payload_size
packet_size = 12 + payload_size
```

The payload size depends on the audio parameters negotiated during handshake.
Audio is typically transmitted in fixed-duration frames:
<<<<<<< HEAD
```
payload_size = (sample_rate × frame_duration_ms × channels × bit_width) / (1000 × 8)
packet_size = sizeof(rtp_header_t) + (sample_rate × frame_duration_ms × channels × bit_width) / (1000 × 8)
```

Where:
- `sample_rate` — Number of audio samples per second (in Hz)
- `channels` — Number of audio channels (1, 2, or 4)
- `bit_width` — Bits per sample (16, 24, or 32)
- `frame_duration_ms` — Frame duration in milliseconds, we will use `frame_duration_ms=10`

Example: 44.1 kHz, Stereo, 16-bit
<<<<<<< HEAD
```
payload_size = (44100 × 10 × 2 × 16) / 8000 = 14112000 / 8000 = 1764 bytes
packet_size = 12 + 1764 = 1776 bytes
```

# Header example

Use [device-protocol.h](./device-protocol.h) header file as reference.

