# Hardware Notes

These notes describe the current low-level assumptions behind the experimental ESP32-C3 driver path.

## RX DMA Registers

| Symbol | Address | Purpose |
| --- | ---: | --- |
| `WIFI_BASE_RX_DSCR` | `0x60033088` | RX descriptor chain base |
| `WIFI_NEXT_RX_DSCR` | `0x6003308c` | Current or next RX descriptor |
| `WIFI_LAST_RX_DSCR` | `0x60033090` | Last descriptor observed by hardware |
| `WIFI_RX_DSCR_A` | `0x60033094` | Additional observed RX descriptor pointer |
| `WIFI_RX_DSCR_B` | `0x60033098` | Additional observed RX descriptor pointer |
| `WIFI_DMA_INT_STATUS` | `0x60033c48` | Observed DMA interrupt or status bits |
| `WIFI_DMA_INT_CLR` | `0x60033c4c` | Observed DMA interrupt clear register |
| `WIFI_DMA_ADDR_MASK_REG` | `0x60033c5c` | Observed reachable DMA address high bound |
| `WIFI_DMA_HW_LOW_REG` | `0x60033c60` | Observed reachable DMA address low bound |
| `WIFI_DMA_CPU_BASE_REG` | `0x60033c64` | Observed CPU base or window helper |

## Current Assumptions

- the Wi-Fi DMA engine appears to consume low hardware addresses derived from internal RAM pointers;
- current code converts CPU pointers with `ptr & 0x000fffff`;
- the heap-padding logic exists because early DMA allocations were observed outside the useful DMA window;
- all undocumented bits should still be treated as experimental.

## PHY and Power Path

The current bring-up path:

- initializes NVS for PHY calibration;
- powers the Wi-Fi domain;
- enables the common PHY clock;
- enables PHY;
- enables and resets the Wi-Fi peripheral.

This path is intentionally smaller than the normal ESP-IDF Wi-Fi startup path because the goal is direct driver control, not association through the official stack.

## Scope Warning

These notes are implementation notes, not hardware facts guaranteed across all ESP32 variants or all ESP-IDF versions.
