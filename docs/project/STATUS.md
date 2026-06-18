# Project Status

Current maturity: **early experimental prototype**.

The project is not yet a complete Wi-Fi driver or a finished Scapy-like packet API. Right now it is the first driver-layer foundation needed to make that packet stack possible.

## What Exists

- ESP32-C3-focused project setup
- Wi-Fi/PHY bring-up without starting the normal Wi-Fi stack
- RX DMA descriptor ring experiments
- experimental ESP32-C3 Wi-Fi register map
- raw RX buffer analysis
- incremental 802.11 frame parser
- packet objects with RSSI metadata
- configurable capture modes for metadata-only, smart, or full frame storage
- circular packet history buffer
- MAC/RX register probing helpers
- register dump tools
- technical documentation under `docs/`

## What Is Missing

- stable packet API
- full packet object model
- packet builders
- controlled TX path
- complete MAC behavior
- clean RX callbacks
- integration with higher-level network layers
- support for ESP32 variants beyond the current ESP32-C3 focus

## Summary

```text
vision:      Scapy-like packet stack for ESP32
current:     ESP32-C3 Wi-Fi RX/DMA/register research base
next step:   turn raw RX and register knowledge into a clean driver API
```

The low-level code exists because it is required for the real goal: complete packet ownership on ESP32.
