# Packet API

This document describes the current packet-facing API exposed by The Packetfather.

## Current Model

Packets are captured from the experimental RX DMA path and parsed into `pf_packet_t` objects.

The current model is intentionally simple:

- no dynamic allocation per packet object;
- fixed-size packet history;
- packet metadata available even after the DMA descriptor is recycled;
- optional full raw-frame retention depending on capture mode.

## Lifecycle

The basic flow is:

```c
pf_init();
pf_set_capture_mode(PF_CAPTURE_MODE_SMART);

const pf_packet_list_t *packets = pf_sniff();
const pf_packet_t *packet = pf_packet_list_get(packets, 0);

pf_sniff_stop();
```

Important points:

- `pf_init()` prepares internal state only.
- `pf_sniff()` starts Wi-Fi/PHY/RX only when sniffing is requested.
- `pf_sniff_stop()` stops RX polling and powers the path down.
- `pf_clear_packets()` resets packet history.

## Packet History

Packet history is a fixed-size circular buffer.

That means:

- old packets are dropped when the buffer is full;
- new packets do not trigger a `memmove`;
- iteration order from `pf_packet_list_get()` is still oldest to newest within the current history window.

Useful fields in `pf_packet_list_t`:

- `count`: number of packets currently available;
- `dropped`: number of packets overwritten in the circular buffer;
- `next_id`: next monotonically increasing packet id.

## Packet Fields

`pf_packet_t` currently exposes:

- `id`
- `raw_len`
- `raw_stored_len`
- `has_raw`
- `has_rssi`
- `rssi`
- `frame_offset`
- `frame_len`
- `payload_offset`
- `frame_control`
- `version`
- `type`
- `subtype`
- `type_name`
- `subtype_name`
- `to_ds`
- `from_ds`
- `retry`
- `protected_frame`
- `has_addresses`
- `addr1`
- `addr2`
- `addr3`
- `has_llc`
- `llc_ethertype`
- `is_eapol`
- `has_ssid`
- `ssid`

## Capture Modes

Capture mode controls whether the driver stores the full raw frame in each packet object.

### `PF_CAPTURE_MODE_METADATA_ONLY`

Only parsed metadata is kept.

Use this when:

- you want lower memory traffic;
- you only need type/subtype/SSID/RSSI and similar parsed fields;
- you do not need to inspect the original bytes later.

### `PF_CAPTURE_MODE_SMART`

Parsed metadata is always kept. Full raw bytes are stored only if the selector says yes.

Use this when:

- you want a general driver;
- some packets need full retention;
- most packets should stay cheap.

### `PF_CAPTURE_MODE_FULL`

Every captured packet stores its full raw bytes up to `PF_PACKET_MAX_RAW_LEN`.

Use this when:

- you are doing parser development;
- you need packet replay or byte-level debugging;
- memory traffic is acceptable.

## Smart Capture Selector

Smart mode is controlled by an application-defined selector:

```c
typedef bool (*pf_capture_selector_t)(const pf_packet_t *packet, void *ctx);
```

Set it with:

```c
pf_set_capture_selector(my_selector, my_ctx);
```

Example:

```c
static bool my_selector(const pf_packet_t *packet, void *ctx)
{
    (void)ctx;

    if (packet->type == PF_PACKET_TYPE_DATA && packet->is_eapol) {
        return true;
    }

    return false;
}
```

This policy is owned by the application, not by the driver. The driver remains general.

## Incremental Parsing

Parsing is intentionally staged:

1. Find the most likely 802.11 frame offset inside the RX buffer.
2. Parse frame control, type, subtype, and core flags.
3. Parse addresses when the frame format supports them.
4. Parse SSID only for supported management frame types.
5. Parse LLC/ethertype only for data frames where the payload layout supports it.
6. Copy the full raw frame only if the current capture mode requires it.

That keeps common sniffing cheaper than always copying and fully decoding every packet.

## Current Helpers

Current helper predicates include:

- `pf_packet_is_type()`
- `pf_packet_is_probe_request()`
- `pf_packet_is_probe_response()`
- `pf_packet_is_data()`
- `pf_packet_is_eapol()`

## Current Limitations

Current limitations of the packet API:

- no TX packet builder layer yet;
- no programmable channel control yet;
- no generic filter API yet beyond application-side inspection;
- no formal callback-based RX delivery API yet;
- no host-side parser tests documented yet.
