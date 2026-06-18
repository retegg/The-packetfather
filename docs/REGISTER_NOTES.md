# Register Notes

The register map is experimental and based on observed ESP32-C3 behavior.

## ESP32-C3 RX DMA

| Symbol | Address | Purpose |
| --- | ---: | --- |
| `WIFI_BASE_RX_DSCR` | `0x60033088` | RX descriptor chain base |
| `WIFI_NEXT_RX_DSCR` | `0x6003308c` | Current/next RX descriptor |
| `WIFI_LAST_RX_DSCR` | `0x60033090` | Last descriptor observed by hardware |
| `WIFI_RX_DSCR_A` | `0x60033094` | Additional observed RX descriptor pointer |
| `WIFI_RX_DSCR_B` | `0x60033098` | Additional observed RX descriptor pointer |
| `WIFI_DMA_INT_STATUS` | `0x60033c48` | Observed DMA interrupt/status bits |
| `WIFI_DMA_INT_CLR` | `0x60033c4c` | Observed DMA interrupt clear register |
| `WIFI_DMA_ADDR_MASK_REG` | `0x60033c5c` | Observed reachable DMA address mask/high bound |
| `WIFI_DMA_HW_LOW_REG` | `0x60033c60` | Observed reachable DMA low bound |
| `WIFI_DMA_CPU_BASE_REG` | `0x60033c64` | Observed CPU base/window helper |

## Notes

- The Wi-Fi DMA engine appears to consume low hardware addresses derived from internal RAM pointers.
- Current code converts CPU pointers with `ptr & 0x000fffff`.
- The heap padding logic exists because early DMA allocations were observed below the useful Wi-Fi DMA window.
- Treat every unknown bit as volatile until confirmed by repeated traces.
