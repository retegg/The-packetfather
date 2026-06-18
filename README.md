# The Packetfather

Experimental open-source Wi-Fi RX/MMIO driver work for ESP32-C3.

The Packetfather is an ESP-IDF project that explores the low-level Wi-Fi receive path on ESP32-C3 without using the normal `esp_wifi_start()` networking stack. It powers the Wi-Fi/PHY block, prepares a DMA RX descriptor ring, attaches that ring to observed Wi-Fi hardware registers, and analyzes raw buffers looking for 802.11 frames.

This is currently research-grade firmware, not a finished production driver. The code is intentionally explicit and heavily instrumented so register behavior can be inspected on real hardware.

## Current Status

- ESP32-C3 target support.
- Low-level Wi-Fi/PHY power-up path.
- Internal DMA-capable RX buffers and circular descriptor chain.
- Experimental RX DMA register attachment.
- RX polling task with descriptor recycling.
- Raw RX analyzer that searches candidate offsets for 802.11 frames.
- Minimal 802.11 frame parser for type, subtype, flags, and MAC addresses.
- Register dump and probing utilities for reverse-engineering.

## Non-Goals

- No packet injection.
- No spoofing.
- No network association.
- No WPA/WPA2 decryption.
- No sockets or IP stack integration.

## Repository Layout

```text
.
|-- CMakeLists.txt          ESP-IDF project definition
|-- Makefile                Optional helper targets
|-- docs/                   Architecture, register notes, and roadmap
|-- main/
|   |-- main.c              Application startup sequence
|   |-- wifi_power.*        Low-level Wi-Fi/PHY power initialization
|   |-- wifi_dma.*          RX DMA descriptor ring and polling
|   |-- wifi_regs.h         Experimental MMIO register map
|   |-- wifi_rx_analyzer.*  Raw buffer to 802.11 frame offset detection
|   |-- wifi_parser.*       Minimal 802.11 frame parser
|   |-- wifi_mac.*          Experimental MAC RX register probing
|   |-- wifi_probe.*        Lightweight register probe helpers
|   `-- wifi_regdump.*      Periodic register dump task
`-- sdkconfig               ESP-IDF project configuration
```

Additional notes live in [`docs/`](docs/):

- [`ARCHITECTURE.md`](docs/ARCHITECTURE.md)
- [`REGISTER_NOTES.md`](docs/REGISTER_NOTES.md)
- [`ROADMAP.md`](docs/ROADMAP.md)
- [`WORKSPACE.md`](docs/WORKSPACE.md)

## Build

Install ESP-IDF, activate its environment, then build for ESP32-C3:

```powershell
idf.py set-target esp32c3
idf.py build
```

Or use the helper Makefile:

```powershell
make build
make flash PORT=COMx
make monitor PORT=COMx
```

Flash and monitor:

```powershell
idf.py -p COMx flash monitor
```

Replace `COMx` with the serial port for your board.

## Hardware Notes

The current register map is based on observed ESP32-C3 behavior and should be treated as experimental. Some addresses and bit meanings may change across ESP-IDF versions, silicon revisions, or target chips.

## Safety and Scope

The project is focused on local receive-path research. It does not transmit frames, modify packets on the air, or connect to networks.

## License

MIT. See [LICENSE](LICENSE).
