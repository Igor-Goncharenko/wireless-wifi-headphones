# Wireless WiFi Headphones

A complete ecosystem for Wi-Fi audio streaming: desktop daemon, ESP32 firmware, and daemon-control interface.


## Overview

This project is a complete implementation of a high-quality, low-latency audio streaming system designed
for wireless headphones and speakers. Unlike traditional Bluetooth solutions, this system leverages Wi-Fi
for higher bandwidth, better audio quality, and more flexible control

### What Makes This Project Special?
- Complete Ecosystem: Desktop daemon, device firmware, and control interface working together
- Wi-Fi Audio Streaming: High bandwidth for uncompressed CD-quality audio (44.1kHz/16-bit)
- Low Latency: Optimized UDP/RTP protocol for real-time audio transmission
- Remote Control: Web interface, CLI, or mobile app for device management
- Open Source: Fully documented protocol specifications for extensibility

### Project Goals
- Create a production-ready audio streaming system for wireless devices
- Establish a well-documented, extensible protocol for Wi-Fi audio
- Provide reference implementations for desktop daemon, ESP32 firmware, and daemon-control interface
- Achieve < 50ms latency with stable, jitter-free audio playback


## System Architecture

The system consists of three main components that work together seamlessly:

```
┌─────────────────────────────────────────────────────────────────┐
│                     Control Interface                           │
│               (Web, CLI, Mobile App, etc.)                      │
│                                                                 │
│  • Device discovery and management                              │
│  • Volume control & playback status                             │
│  • Real-time updates via WebSocket                              │
│  • Cross-platform (browser, terminal, mobile)                   │
└──────────────────────────┬──────────────────────────────────────┘
                           │
                           │ Control API (REST)
                           │ (JSON over HTTP)
                           ↓
┌─────────────────────────────────────────────────────────────────┐
│                      Desktop Daemon                             │
│                                                                 │
│  • Device discovery (UDP broadcast)                             │
│  • Session management & handshake                               │
│  • Command processing (volume, battery, etc.)                   │
│  • Audio packet routing (RTP)                                   │
│  • WebSocket server for UI                                      │
│  • Audio device output (optional)                               │
│  • Multi-device management                                      │
└──────────────────────────┬──────────────────────────────────────┘
                           │
                           │ Device Protocol (UDP/TCP)
                           │ (Binary, packed structures)
                           ↓
┌─────────────────────────────────────────────────────────────────┐
│                     Edge Device                                 │
│               (Headphones, Speaker, Receiver)                   │
│                                                                 │
│  • Network connectivity (Wi-Fi/Ethernet)                        │
│  • Device discovery responder                                   │
│  • Handshake & session negotiation                              │
│  • Command handling (volume, battery monitoring)                │
│  • RTP audio reception & buffering                              │
│  • Audio output (DAC, I2S, analog)                              │
│  • User input handling (buttons, touch)                         │
│  • Status indicators (LEDs, display)                            │
└─────────────────────────────────────────────────────────────────┘
```


## API Overview

This section describes the two core APIs that define communication between system components.

### 1. Device Protocol API

The Device Protocol API defines the binary-level communication protocol between the desktop daemon
and edge devices (headphones, speakers, receivers). This low-level protocol handles device
discovery, session negotiation, command exchange, and real-time audio streaming over UDP and TCP.
It is designed to be lightweight, deterministic, and suitable for resource-constrained embedded
systems, with packed binary structures, minimal overhead, and support for extensibility through
reserved fields.

The protocol operates in four distinct phases: **Discovery** (UDP broadcast to locate devices),
**Handshake** (TCP session negotiation with audio parameter exchange), **Command Exchange** (TCP for
control commands like volume and battery queries), and **Audio Streaming** (UDP/RTP for real-time
audio transmission). All non-RTP packets share a common header with magic number validation,
versioning, and length fields to ensure protocol integrity and forward compatibility.

**Full Documentation:** [Device API Specification](./docs/device-api/DEVICEAPI.md)

### 2. Daemon Control API

The Daemon Control API provides a high-level, RESTful interface for controlling the audio system from
any client application—web interfaces, CLI tools, mobile apps, or third-party integrations. Built on
HTTP protocol with JSON payloads, this API abstracts away the complexity of the underlying device
protocol, offering a clean, developer-friendly way to discover devices, manage connections, control
audio settings, and receive real-time updates about device status changes.

The API is organized into logical resource groups: **System** (daemon status and version),
**Devices** (listing, connecting, disconnecting), and **Device Control** (volume, battery, audio
parameters). **Real-time events** — such as device discovery, connection changes, volume updates, and
battery notifications—are pushed to clients via WebSocket connections, enabling responsive user
interfaces without continuous polling. The API follows RESTful conventions with standard HTTP
methods, status codes, and includes OpenAPI/Swagger documentation for automatic client generation.

**Full Documentation:** [Daemon API Specification](./docs/daemon-api/DAEMONAPI.md)


## Known Challenges & Limitations

This section outlines the key technical challenges and limitations of the current system.
These are inherent to Wi-Fi audio streaming and represent active areas of development.

### 1. Latency
Achieving low-latency (< 50ms) audio over Wi-Fi is fundamentally challenging due to network stack
processing overhead, variable network transmission times, and the trade-off between jitter buffer
size and latency. The buffering required to handle network jitter directly increases end-to-end
latency, making real-time applications like gaming particularly difficult.

### 2. Jitter & Packet Loss
Network packet arrival times vary significantly in real-world conditions, causing audio glitches
and instability. Without robust mechanisms to handle packet loss and timing variations, audio
quality degrades noticeably. The current approach to jitter buffering requires a careful balance
between stability and latency.

### 3. Power Consumption
Wi-Fi communication consumes substantially more power than Bluetooth Low Energy (BLE) — typically
10-20 times higher during active streaming. This significantly limits battery life compared to
commercial Bluetooth audio products, requiring larger batteries or more aggressive power management
strategies.

### 4. Network Congestion
Wi-Fi networks are shared mediums susceptible to interference and congestion, especially in dense
urban environments with many competing devices. Audio streaming over UDP provides no congestion
control or adaptive bitrate mechanisms, potentially leading to degraded audio quality during
network congestion.

### 5. Security
The protocol currently provides no encryption, authentication, or integrity checks. All
communication—including commands and audio data—is transmitted in plaintext over the network.
This exposes the system to eavesdropping, unauthorized access, command injection, and replay attacks.

### 6. Setup & Configuration
Network configuration currently requires manual intervention. The system lacks zero-touch
provisioning, automatic network discovery (mDNS/DNS-SD), and user-friendly setup workflows.
This creates a barrier to entry for non-technical users.

### 7. Audio Quality vs. Performance Trade-offs
Higher sample rates, bit depths, and channel counts require significantly more bandwidth and
processing power. Achieving CD-quality (44.1kHz/16-bit) or better audio while maintaining low
latency and stable playback requires careful optimization and may not be feasible on all hardware
platforms.


## License

This project is licensed under the GPLv3 License - see the [LICENSE](LICENSE) file for details.

