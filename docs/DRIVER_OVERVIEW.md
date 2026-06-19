# Driver Overview

The Packetfather is an experimental open Wi-Fi driver and packet stack for ESP32-C3.

The project goal is not just to sniff frames. The goal is to build a driver surface that can eventually support complete packet ownership on ESP32, in a way that feels closer to Scapy than to a normal embedded networking example.

## Current Flow

The current runtime flow is:

1. `pf_init()` prepares internal state.
2. `pf_sniff(primary_channel)` starts the Wi-Fi/PHY/RX path on demand.
3. `wifi_power_enable_minimal()` powers the Wi-Fi domain and PHY path without starting the normal ESP-IDF Wi-Fi stack.
4. `wifi_dma_rx_setup()` prepares DMA descriptors and RX buffers.
5. `wifi_dma_rx_attach_to_hardware()` points the observed Wi-Fi DMA registers at the descriptor ring.
6. `wifi_dma_rx_start_polling()` starts the RX polling task.
7. RX buffers are parsed into `pf_packet_t` objects and placed in packet history.
8. Readers should consume packet history through metadata copies, not by iterating over a live writer-owned buffer.
9. `pf_sniff_stop()` stops polling and powers the path down again.

This keeps Wi-Fi off until packet capture is actually requested.

## Current Modules

`pf.*`

Public entry point for initialization, sniff start/stop, debug mode, capture mode, capture selection policy, and channel requests.

`packet.*`

Packet model, parser, helper predicates, and packet-history storage.

`wifi_power.*`

Low-level power and PHY enable/disable path.

`wifi_dma.*`

RX DMA descriptor setup, RX polling, descriptor recycling, and packet capture handoff.

`wifi_regs.h`

Observed ESP32-C3 register definitions used by the current driver experiments.


## Current Design Direction

The code is moving toward this shape:

```text
packet tooling / high-level packet control
        |
packet objects, builders, filters, callbacks
        |
open Wi-Fi driver surface
        |
DMA, MAC, RX/TX control, PHY bring-up
        |
ESP32 hardware
```

The low-level code exists because the real target is full packet control, not just register experiments.

## Current Constraints

- ESP32-C3 only
- RX path is the current focus
- TX path is not yet exposed as a clean driver API
- packet history is polling-based, not callback-based
- channel retune backend is experimental, but current sniff mode can request fixed-channel capture or channel hopping
