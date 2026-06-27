# Hardware Notes

These notes are for driver internals.

You do not need this file to use `Beacon()`, `ProbeRequest()`, `pf_tx_raw()`, or the RX packet API.

Read this when working on:

- DMA descriptors;
- ESP32-C3 Wi-Fi registers;
- PHY/power bring-up;
- native TX/RX behavior.

## Warning

These are observed ESP32-C3 notes, not official hardware documentation.

Treat every undocumented bit as experimental.

Behavior may change across:

- ESP32 variants;
- ESP-IDF versions;
- PHY calibration state;
- startup sequence changes.

## RX DMA Registers

These registers are used by the current RX experiments.

| Symbol | Address | What It Appears To Do |
| --- | ---: | --- |
| `WIFI_BASE_RX_DSCR` | `0x60033088` | RX descriptor chain base |
| `WIFI_NEXT_RX_DSCR` | `0x6003308c` | current/next RX descriptor |
| `WIFI_LAST_RX_DSCR` | `0x60033090` | last descriptor observed by hardware |
| `WIFI_RX_DSCR_A` | `0x60033094` | additional observed RX descriptor pointer |
| `WIFI_RX_DSCR_B` | `0x60033098` | additional observed RX descriptor pointer |
| `WIFI_DMA_INT_STATUS` | `0x60033c48` | DMA interrupt/status bits |
| `WIFI_DMA_INT_CLR` | `0x60033c4c` | DMA interrupt clear register |
| `WIFI_DMA_ADDR_MASK_REG` | `0x60033c5c` | observed DMA address high bound |
| `WIFI_DMA_HW_LOW_REG` | `0x60033c60` | observed DMA address low bound |
| `WIFI_DMA_CPU_BASE_REG` | `0x60033c64` | observed CPU base/window helper |

## RX Address Assumption

The current code maps CPU pointers into the Wi-Fi DMA-visible address range using:

```c
ptr & 0x000fffff
```

This is based on observed ESP32-C3 behavior.

It is not a general rule for every ESP32 chip.

## Why Heap Padding Exists

Some early DMA allocations landed outside the useful observed DMA window.

The heap-padding logic exists to push later allocations into addresses that the Wi-Fi DMA engine appeared to consume correctly.

This is a workaround, not a final design.

## PHY And Power Bring-Up

The current custom bring-up path does a smaller version of what the official Wi-Fi stack normally hides:

1. initialize NVS for PHY calibration;
2. power the Wi-Fi domain;
3. enable common PHY clock;
4. enable PHY;
5. enable/reset Wi-Fi peripheral;
6. configure the custom RX/TX path.

The project intentionally does not start the normal ESP-IDF Wi-Fi driver for the native path.

## TX Notes

The current native TX path is experimental.

It uses observed:

- TX slots;
- DMA descriptors;
- PLCP-related registers;
- completion/error status registers.

The important split is:

```text
802.11 frame length != hardware/radio air length
```

Packet builders provide the 802.11 frame bytes and length.

The TX backend must translate that into whatever descriptor, PLCP, DMA, and RF state the hardware expects.

## Reference Project

Much of the reverse-engineering direction is inspired by:

- `esp32-open-mac/esp32-open-mac`

That project is a major reference for open MAC work, ESP32 Wi-Fi register exploration, TX/RX behavior, and low-level packet control ideas.
