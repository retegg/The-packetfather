# Packet API

This document describes the current packet-facing API.

## Core Types

The main object is `pf_packet_t`.

Current packet objects expose:

- packet id
- RSSI when available
- channel when available from RX metadata or management frame tags
- frame offset and frame length
- frame control metadata
- 802.11 type and subtype
- address fields when present
- SSID for supported management frames
- LLC ethertype for supported data frames
- `is_eapol` classification helper
- optional retained raw bytes

For normal application reads, use `pf_packet_metadata_t`. It mirrors the parsed fields without carrying the raw byte buffer.

Packet lists are represented by `pf_packet_list_t`.

The list is a fixed-size circular buffer:

- new packets do not shift existing memory;
- old packets are overwritten when capacity is reached;
- `dropped` counts overwritten entries.

## Sniffing API

The current public entry points are:

```c
bool pf_init(void);
void pf_debug(bool enabled);
void pf_set_capture_mode(pf_capture_mode_t mode);
void pf_set_capture_selector(pf_capture_selector_t selector, void *ctx);
bool pf_set_channel(uint8_t primary, pf_secondary_channel_t secondary);
pf_channel_config_t pf_get_channel_config(void);
const pf_packet_list_t *pf_sniff(uint8_t primary_channel);
void pf_sniff_stop(void);
void pf_clear_packets(void);
size_t pf_packet_history_count(void);
bool pf_packet_history_get_metadata(size_t index, pf_packet_metadata_t *out);
bool pf_packet_history_get_copy(size_t index, pf_packet_t *out);
```

`pf_sniff(0)` requests channel hopping.

`pf_sniff(n)` with `n >= 1` requests a fixed primary channel.

## Reading Packets Safely

The RX polling task writes packet history concurrently.

Applications should read packet history through metadata copies:

```c
pf_packet_metadata_t packet;
size_t count;

pf_sniff(0);
count = pf_packet_history_count();

for (size_t i = 0; i < count; i++) {
    if (!pf_packet_history_get_metadata(i, &packet)) {
        continue;
    }

    if (packet.type == PF_PACKET_TYPE_MANAGEMENT) {
        /* inspect parsed metadata */
    }
}
```

`pf_packet_history_get_copy()` copies a full `pf_packet_t`, including the raw storage window. Use it only when the application really needs retained raw bytes.

Do not iterate over a live writer-owned history buffer from another task.

## Capture Modes

`PF_CAPTURE_MODE_METADATA_ONLY`

Only parsed metadata is stored in the packet object.

`PF_CAPTURE_MODE_SMART`

The packet is parsed first, then a user-defined selector decides whether the full raw capture should also be stored.

`PF_CAPTURE_MODE_FULL`

Raw bytes are copied into the packet object up to `PF_PACKET_MAX_RAW_LEN`.

## Parse Limit vs Raw Limit

Parsing and raw retention are intentionally separate.

- `PF_PACKET_MAX_PARSE_LEN` limits how much incoming data the parser will inspect.
- `PF_PACKET_MAX_RAW_LEN` limits how many raw bytes are retained inside `pf_packet_t`.

That means metadata parsing is not forced to fail just because the retained raw window is smaller than the incoming buffer.

## Smart Capture Policy

Smart mode is intentionally general.

The driver does not hardcode packet policies like "always keep EAPOL" or "always keep beacons". Instead, the application provides the decision function:

```c
typedef bool (*pf_capture_selector_t)(const pf_packet_t *packet, void *ctx);
```

Example:

```c
static bool my_selector(const pf_packet_t *packet, void *ctx)
{
    (void)ctx;

    return packet->type == PF_PACKET_TYPE_DATA && packet->is_eapol;
}
```

That example is just one possible policy. The driver stays neutral.

## Parsing Strategy

The parser is incremental.

Current parsing stages are:

1. find the most likely 802.11 frame offset inside the RX buffer;
2. parse frame control, type, subtype, and flags;
3. parse addresses when the frame layout supports them;
4. parse SSID and DS channel tags for supported management frames;
5. parse LLC ethertype for supported data frames;
6. retain the full raw bytes only if the current capture mode requires it.

This avoids paying the full cost for every packet when only metadata is needed.

## Current Helpers

Current helper predicates include:

- `pf_packet_is_type()`
- `pf_packet_is_probe_request()`
- `pf_packet_is_probe_response()`
- `pf_packet_is_data()`
- `pf_packet_is_eapol()`

## Missing API Pieces

The packet layer still needs:

- generic filter API
- RX callbacks
- TX packet builders
- cleaner higher-level packet classification
- host-side parser tests
