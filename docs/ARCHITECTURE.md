# Architecture

The Packetfather is split into small modules so each hardware experiment has a clear boundary.

## Startup Flow

`app_main()` in `main/main.c` calls `pf_init()`, then reads packets with `pf_sniff()`. `pf_init()` prepares stack state only; Wi-Fi starts on demand when sniffing begins.

1. `pf_init()` clears packet history and marks the packet stack ready.
2. `pf_sniff()` starts sniffing if it is not already active.
3. `wifi_power_enable_minimal()` initializes NVS, powers the Wi-Fi domain, enables the PHY clock, enables PHY, and resets the Wi-Fi peripheral.
4. `wifi_dma_rx_setup()` allocates DMA-capable descriptors and buffers, then links them into a circular RX chain.
5. `wifi_dma_rx_attach_to_hardware()` writes the descriptor base/current pointers to the observed Wi-Fi DMA registers.
6. `wifi_dma_rx_start_polling()` starts the RX task.
7. `wifi_dma_rx_poll_task()` polls descriptors for received data, captures them as `pf_packet_t` objects, and recycles descriptors.
8. `pf_sniff_stop()` stops polling and powers the Wi-Fi block down.

## Modules

`wifi_regs.h`
: Experimental register map. Keep target-specific MMIO addresses here instead of duplicating literals across modules.

`pf.*`
: Public API for initialization, starting sniffing, stopping sniffing, packet history access, and capture-mode configuration.

`wifi_power.*`
: Low-level power and PHY bring-up. This deliberately does not start the normal ESP-IDF Wi-Fi driver.

`wifi_dma.*`
: DMA descriptor allocation, descriptor ring management, register attachment, polling, and descriptor recycling.
  The descriptor chain is allocated once and rearmed on later sniffing sessions.
  The polling task now uses a simple active/idle backoff so it does less useless work when no packets arrive.

`packet.*`
: Packet object model, incremental parsing, optional raw-frame retention, helper predicates, and circular packet history.

`wifi_mac.*`, `wifi_probe.*`, `wifi_regdump.*`
: Reverse-engineering helpers for inspecting MAC/RX registers and probing candidate bits.

## Sniffing Lifecycle

`pf_init()` does not power Wi-Fi. It only initializes packet stack state.

`pf_sniff()` starts the Wi-Fi/RX path when needed:

1. Power/PHY bring-up.
2. RX DMA descriptor setup or rearm.
3. RX DMA register attachment.
4. RX polling task start.
5. Packet history returned to the caller.

`pf_sniff_stop()` stops the polling task and disables the Wi-Fi/PHY path as far as the current experimental driver can do safely.

## Packet History

The first packet layer is intentionally small and C-friendly:

```c
pf_init();

const pf_packet_list_t *packets = pf_sniff();
const pf_packet_t *packet = pf_packet_list_get(packets, 0);

if (packet != NULL && strcmp(packet->subtype_name, "Beacon") == 0) {
    /* string-style filtering */
}
```

Each stored packet owns parsed metadata. Full raw bytes are stored only when the current capture mode requires it.

The current packet layer supports:

- metadata-only capture for cheaper sniffing;
- smart capture driven by an application callback;
- full raw capture for parser development and byte-level inspection;
- RSSI attached to packet objects from RX metadata.

## Capture Decisions

Capture mode is owned by `pf.*`.

- `PF_CAPTURE_MODE_METADATA_ONLY` keeps packet retention cheap.
- `PF_CAPTURE_MODE_SMART` asks a user-defined selector whether a parsed packet should keep its raw bytes.
- `PF_CAPTURE_MODE_FULL` always stores raw bytes.

This keeps the driver general. The driver parses enough metadata to classify packets first, then decides whether a full copy is worth the cost.

## Design Rules

- Do not duplicate MMIO register addresses outside `wifi_regs.h`.
- Keep experiments slow, observable, and reversible. Low-level register discovery should change one thing at a time.
- Prefer general mechanisms over hardcoded packet policies. Application code should decide which packets deserve expensive handling.
- Prefer clear logs over clever control flow when debug is enabled. Normal runtime should stay quiet.
