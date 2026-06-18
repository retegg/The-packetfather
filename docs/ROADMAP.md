# Roadmap

This roadmap describes the path from the current prototype to an open ESP32 packet stack with driver-level packet control.

## Milestone 1: Repository Hygiene

- Add README, license, ignore rules, and formatting config.
- Keep generated ESP-IDF build artifacts out of Git.
- Centralize experimental register definitions.
- Document current limitations honestly.

## Milestone 2: RX Stability

- Confirm the DMA register map across multiple ESP32-C3 boards.
- Replace polling-only RX handling with interrupt-driven RX if the interrupt bits are fully understood.
- Validate descriptor layout against hardware writes.
- Add runtime checks for invalid DMA lengths and unreachable buffers.

## Milestone 3: Frame Parsing

- Store captured frames as `pf_packet_t` objects.
- Keep a fixed-size packet history for inspection after DMA recycling.
- Parse beacon SSIDs and supported rates.
- Parse common management, control, and data subtypes.
- Add host-side parser tests using captured raw buffers.

Progress so far:

- `pf_packet_t` exists.
- packet history exists and now uses a circular buffer.
- management SSID parsing exists.
- RSSI is attached to packet objects.
- LLC ethertype and `is_eapol` metadata exist for data frames.
- capture can now be metadata-only, smart, or full.

## Milestone 4: Packet API

- Add packet builders for common 802.11 frames.
- Add helpers for filters and comparisons.
- Define a small RX callback API.
- Separate research probes from the stable driver surface.
- Add examples for passive scan/sniffing experiments.
- Document ESP-IDF version and target compatibility.

Next API-focused work:

- general packet filter API;
- channel control API;
- callback-based delivery instead of history polling only;
- packet-builder side for TX experiments;
- better packet classification beyond current subtype and LLC parsing.
