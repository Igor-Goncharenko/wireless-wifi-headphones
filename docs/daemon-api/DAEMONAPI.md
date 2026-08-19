# Daemon API Specification

The Daemon Control API provides a high-level, RESTful interface for controlling the Wi-Fi audio
streaming system from any client application—web interfaces, CLI tools, mobile apps, or third-party
integrations. Built on HTTP with JSON payloads, this API abstracts away the complexity of the
underlying device protocol, offering a clean, developer-friendly way to discover devices, manage
connections, control audio settings, and receive real-time updates about device status changes.

# Architecture Context

This API is the primary interface for all client applications that need to interact with the audio
streaming system. It sits between the user interface layer and the desktop daemon, which in turn
communicates with edge devices (headphones, speakers, receivers) over the Device Protocol.

# Key Features

- Device Discovery & Management — Automatically discover devices on the network, list them, and
  manage connections
- Audio Control — Adjust volume, monitor battery status, and retrieve audio parameters
- Real-time Events — Receive immediate notifications about device discovery, connection changes,
  volume updates, and battery status via WebSocket
- Multi-device Support — Connect to and manage multiple devices simultaneously
- RESTful Design — Follows standard HTTP conventions with predictable endpoints and response codes
- Consistent Error Handling — All errors follow a unified format with machine-readable codes

# Base URL

All endpoints are relative to the base URL:

```
http://localhost:8080/api/v1
```

# Content Type

All requests and responses use application/json unless otherwise specified.

# API Resources

The API is organized into four logical resource groups:

## 1. System Operations

Monitor the health and status of the daemon itself.

| Operation | Endpoint         | Description                                 |
| :-------- | :--------------- | :------------------------------------------ |
| GET       | `/system/status` | Retrieve daemon version, status, and uptime |

## 2. Device Management

Discover and manage audio devices on the network.

| Operation | Endpoint                          | Description                 |
| :-------- | :-------------------------------- | :-------------------------- |
| GET       | `/devices`                        | List all discovered devices |
| POST      | `/devices/{device_id}/connect`    | Connect to a device         |
| POST      | `/devices/{device_id}/disconnect` | Disconnect from a device    |
| GET       | `/devices/{device_id}/battery`    | Get battery status          |

## 3. Audio Control

Control audio playback settings on connected devices.

| Operation | Endpoint                      | Description        |
| :-------- | :---------------------------- | :----------------- |
| GET       | `/devices/{device_id}/volume` | Get current volume |
| PUT       | `/devices/{device_id}/volume` | Set volume level   |

## 4. WebSocket Events

Receive real-time notifications about system events.

| Operation | Endpoint | Description                               |
| :-------- | :------- | :---------------------------------------- |
| GET       | `/ws`    | Establish WebSocket connection for events |

# Real-time Events via WebSocket

The API provides a WebSocket endpoint for receiving real-time event notifications. This is essential
for building responsive user interfaces that react immediately to changes without continuous
polling.

## Event Types

| Event Type          | Payload Schema | Description                         |
| :------------------ | :------------- | :---------------------------------- |
| `device_discovered` | Device         | New device found on the network     |
| `device_lost`       | Device         | Device no longer responding         |
| `device_updated`    | Device         | Device properties changed           |
| `volume_changed`    | VolumeData     | Volume changed (from API or device) |
| `battery_updated`   | BatteryData    | Battery status updated              |
| `error`             | ErrorData      | Error occurred                      |

# Error Handling

All errors follow a consistent format, making error handling predictable across all endpoints. Error
Response Format

```json
{
  "code": "DEVICE_NOT_FOUND",
  "message": "Device A1B2C3D4E5F6 not found",
  "timestamp": "2026-08-18T10:30:00.123Z",
  "device_id": "A1B2C3D4E5F6",
  "details": {
    "field": "device_id",
    "value": "A1B2C3D4E5F6"
  }
}
```

## Error Codes

| Code                       | Description       | When It Occurs                                 |
| :------------------------- | :---------------- | :--------------------------------------------- |
| `INVALID_DEVICE_ID`        | Invalid format    | Device ID not 12 hex characters                |
| `DEVICE_NOT_FOUND`         | Device not found  | Device ID doesn't exist                        |
| `DEVICE_ALREADY_CONNECTED` | Already connected | Attempting to connect already connected device |
| `DEVICE_NOT_CONNECTED`     | Not connected     | Operation requires connected device            |
| `INTERNAL_ERROR`           | Server error      | Unexpected error in daemon                     |
| `UNKNOWN_ERROR`            | Unspecified error | Other error conditions                         |

## HTTP Status Codes

| Status  | Description    | Common Causes                                            |
| :------ | :------------- | :------------------------------------------------------- |
| 200     | Success        | Operation completed successfully                         |
| 400     | Bad Request    | Invalid DeviceID, malformed JSON, out-of-range volume    |
| 404     | Not Found      | Device doesn't exist                                     |
| 409     | Conflict       | Device in wrong state (already connected, not connected) |
| 500     | Internal Error | Unexpected server error                                  |
| default | Other          | Any undocumented HTTP error                              |

# Limitations and Considerations

## Network Latency

The API is designed for local network usage (< 50ms roundtrip). Wide-area network usage may
experience higher latency and should be tested thoroughly.

## Concurrent Connections

The daemon supports multiple simultaneous client connections. However, audio streaming is typically
limited to one active device at a time.

## Device Availability

Devices are discovered via UDP broadcast and may disappear from the list if they go offline or
change networks.

## Volume Precision

Volume is specified as an integer (0-100). The device firmware may internally map this to its
specific volume range.

## Battery Reporting

Not all devices support battery monitoring. The battery field will be null when unavailable.

## WebSocket Reconnection

WebSocket connections may drop due to network issues. Implement reconnection logic with exponential
backoff for robust applications.

# API Specification

The complete OpenAPI specification is available in [daemon-api-v1.yaml](./daemon-api-v1.yaml).
