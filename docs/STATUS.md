# Project Status

Current maturity: **early experimental prototype**.

The project already has a real RX path and a packet model, but it is still far from a complete open Wi-Fi driver.

## What Exists Today

- ESP32-C3-focused codebase
- Wi-Fi/PHY bring-up without normal ESP-IDF Wi-Fi startup
- RX DMA descriptor ring setup
- packet capture into `pf_packet_t`
- RSSI attached to packet objects
- metadata-only, smart, and full capture modes
- circular packet history
- incremental packet parsing
- debug gating through `pf_debug()`

## What Does Not Exist Yet

- clean TX path
- channel control API
- generic filter API
- callback-based RX delivery
- stable packet-builder layer
- support beyond the current ESP32-C3 focus

## Current Position

```text
goal:       full packet control on ESP32
current:    experimental RX driver foundation
next:       cleaner driver APIs, filters, channels, TX work
```
