# Driver Overview

This page explains how the project is shaped.

It is not a register reference. For register notes, read [HARDWARE_NOTES.md](HARDWARE_NOTES.md).

## One Sentence

The Packetfather is trying to become a Scapy-like Wi-Fi packet stack that runs directly on ESP32-C3.

## Big Picture

```text
your app
  |
packet builders / packet parser / filters
  |
Packetfather public API
  |
native ESP32-C3 RX/TX driver experiments
  |
Wi-Fi DMA, MAC registers, PHY/power setup
  |
ESP32-C3 hardware
```

The high-level goal is packet control.

The low-level driver work exists so packet control can be real, not just a wrapper around the official Wi-Fi API.

## TX Flow

TX means sending packets.

```text
Beacon()
  |
pf_frame_t
  |
pf_tx_raw()
  |
native TX backend
  |
ESP32-C3 radio
```

The builder creates normal 802.11 frame bytes.

The TX backend handles the ESP32-C3 hardware path.

Current TX builders:

- `Beacon()`
- `ProbeRequest()`
- `ProbeResponse()`
- `Deauth()`

## RX Flow

RX means receiving/sniffing packets.

```text
ESP32-C3 radio
  |
Wi-Fi DMA buffers
  |
packet parser
  |
packet history
  |
metadata copies for your app
```

Application code should read captured packets through copy APIs, not by directly walking live driver-owned memory.

## Main Modules

| Module | Purpose |
| --- | --- |
| `main.c` | current lab example |
| `pf.*` | public driver API: init, sniff, channel, capture mode, network discovery |
| `packet.*` | packet model, parser, metadata, packet history |
| `packet_builder.*` | Scapy-like TX builders |
| `network.*` | passive AP/network inventory |
| `tx.*` | generic raw TX API |
| `wifi_tx.*` | experimental native TX backend |
| `wifi_dma.*` | RX DMA descriptors, buffers, polling |
| `wifi_power.*` | Wi-Fi/PHY power bring-up |
| `wifi_regs.h` | observed ESP32-C3 register map |

## What Uses ESP-IDF

The project avoids starting normal `esp_wifi` for the custom driver path, but it still uses ESP-IDF for useful platform pieces:

- project build system;
- FreeRTOS tasks and delays;
- logging;
- NVS/PHY calibration support;
- MAC address reading through `esp_read_mac()`;
- low-level SoC/peripheral helpers where needed.

## What Does Not Use `esp_wifi_80211_tx()`

The current native TX path does not call:

```c
esp_wifi_80211_tx()
```

Packetfather builds the frame and sends it through the experimental ESP32-C3 MMIO/DMA TX path.

That is the point of the project: learn and control the lower layers.

## Current Constraints

- ESP32-C3 is the active target.
- TX is visible in Wireshark but still experimental.
- RX exists, but delivery is currently polling/history based.
- Channel handling is still experimental.
- Builders currently cover out-of-network management frames.

## Where To Start Editing

| Goal | File |
| --- | --- |
| Add a new packet builder | `main/packet_builder.c` and `main/packet_builder.h` |
| Change raw TX behavior | `main/tx.c`, `main/wifi_tx.c`, `main/wifi_tx.h` |
| Improve packet parsing | `main/packet.c`, `main/packet.h` |
| Improve RX DMA behavior | `main/wifi_dma.c`, `main/wifi_dma.h` |
| Add public API | `main/pf.c`, `main/pf.h` |
