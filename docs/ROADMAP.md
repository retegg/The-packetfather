# Roadmap

This roadmap describes the path from the current research prototype to a cleaner open-source ESP32-C3 Wi-Fi RX driver.

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

- Parse beacon SSIDs and supported rates.
- Parse common management, control, and data subtypes.
- Add structured frame metadata instead of log-only output.
- Add host-side parser tests using captured raw buffers.

## Milestone 4: Public Driver API

- Define a small RX callback API.
- Separate research probes from the stable driver surface.
- Add examples for passive scan/sniffing experiments.
- Document ESP-IDF version and target compatibility.
