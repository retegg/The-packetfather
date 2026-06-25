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
- generic raw TX API through `pf_tx_raw()`
- experimental native ESP32-C3 TX backend using MMIO slots, DMA descriptors, and PLCP registers

## What Does Not Exist Yet

- validated hardware channel retune backend
- generic filter API
- callback-based RX delivery
- stable packet-builder layer
- support beyond the current ESP32-C3 focus

## Current Position

```text
goal:       full packet control on ESP32
current:    experimental RX/TX driver foundation
next:       validate native TX completion/error handling, cleaner driver APIs, filters, connection work
```
