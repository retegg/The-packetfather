# Architecture

The Packetfather is split into small modules so each hardware experiment has a clear boundary.

## Startup Flow

`app_main()` in `main/main.c` runs the current RX experiment:

1. `wifi_power_enable_minimal()` initializes NVS, powers the Wi-Fi domain, enables the PHY clock, enables PHY, and resets the Wi-Fi peripheral.
2. `wifi_dma_rx_setup()` allocates DMA-capable descriptors and buffers, then links them into a circular RX chain.
3. `wifi_dma_rx_attach_to_hardware()` writes the descriptor base/current pointers to the observed Wi-Fi DMA registers.
4. `wifi_dma_rx_kick_hardware()` toggles the experimental RX trigger bit.
5. `wifi_dma_rx_poll_task()` polls descriptors for received data and recycles them after analysis.

## Modules

`wifi_regs.h`
: Experimental register map. Keep target-specific MMIO addresses here instead of duplicating literals across modules.

`wifi_power.*`
: Low-level power and PHY bring-up. This deliberately does not start the normal ESP-IDF Wi-Fi driver.

`wifi_dma.*`
: DMA descriptor allocation, descriptor ring management, register attachment, polling, and descriptor recycling.

`wifi_rx_analyzer.*`
: Searches raw DMA buffers for a plausible 802.11 frame offset.

`wifi_parser.*`
: Minimal 802.11 parser used after the analyzer finds a candidate frame.

`wifi_mac.*`, `wifi_probe.*`, `wifi_regdump.*`
: Reverse-engineering helpers for inspecting MAC/RX registers and probing candidate bits.

## Design Rules

- Do not duplicate MMIO register addresses outside `wifi_regs.h`.
- Keep transmit/injection code out of this project unless the repository scope is explicitly changed.
- Keep experiments slow, observable, and reversible. Low-level register discovery should change one thing at a time.
- Prefer clear logs over clever control flow. Most value currently comes from comparing live hardware traces.
