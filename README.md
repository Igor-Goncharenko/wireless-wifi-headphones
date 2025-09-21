# Wireless WiFi Headphones

A groundbreaking open-source project to build truly wireless stereo
full-size headphones that communicate over Wi-Fi, using the powerful 
ESP32 microcontroller. This project explores the boundaries of 
low-latency, high-quality audio streaming without relying on Bluetooth.

> **⚠️ Important Note: Experimental Project**
> This is a **Work in Progress** and a highly complex project. Achieving
> low-latency, synchronized audio over Wi-Fi is challenging. Expect to
> encounter issues like latency, jitter, and sync drift. This repository
> is for educational purposes and for developers interested in audio
> streaming protocols.

## Key Features & Goals

- **Wi-Fi Audio Streaming:** Transmit stereo audio data over your local network instead of Bluetooth.
- **Low Latency Protocol:** Utilizes UDP and Real-Time Transport Protocol (RTP) for faster transmission than TCP.
- **I2S Audio Output:** High-quality audio output via the ESP32's I2S peripheral to a DAC and amplifier.
- **Sample Rate & Depth:** Target 44.1 kHz / 16-bit CD-quality audio (subject to bandwidth and latency constraints).
- **Server-Client Architecture:** A Python server runs on your PC, streaming audio directly to the headphones.

## Hardware Requirements

### For the Headphones (Client)

- **1x ESP32 Dev Modules** (e.g., ESP32-WROOM-32)
- **1x I2S DAC Amplifiers** (e.g., MAX98357A).
- **2x Speakers / Headphone Drivers** (e.g., 40mm dynamic drivers, 32Ω impedance).
- **1x Li-ion Battery** (e.g., 3.7V 1000mAh).
- **2x Battery Charging/Protection Modules** (e.g., TP4056).
- **Wires, Switches, and a 3D-Printed Enclosure.**

### For the Audio Source (Server)

- A computer (Windows, Linux, or macOS) with Python.

## How It Works

1.  **Audio Capture:** The Python server on your PC captures system audio using `sounddevice` or `pyaudio`.
2.  **Packetization:** The audio stream is packaged into UDP packets, often with an RTP header for timing information.
3.  **Streaming:** Packets are continuously streamed to the "master" ESP32 on your local network.
4.  **Reception & Processing:** The master ESP32 receives the packets, decodes the audio data, and handles the synchronization protocol.
5.  **I2S Output:** The master ESP32 sends the audio data to its own I2S DAC and also relays the data to the "slave" ESP32 (for the other channel) via a secondary Wi-Fi link or ESP-NOW.
6.  **Amplification:** The DACs convert the digital signal to analog, which is then amplified and played through the speakers.

## Known Challenges & Limitations

- **Latency:** The primary challenge. Wi-Fi stack processing, buffering, and network transmission can introduce significant delay (>50ms), making it unsuitable for real-time applications like gaming without advanced techniques.
- **Jitter:** Network packet arrival time variation can cause audio glitches without a sophisticated jitter buffer.
- **Power Consumption:** Wi-Fi is significantly more power-hungry than Bluetooth Low Energy (BLE). Battery life will be much shorter than commercial products.
- **Error Handling:** The current implementation is a basic proof-of-concept. It lacks robust error correction for lost packets.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

