# Packet API

This page explains how to use packets in The Packetfather.

There are two sides:

- TX: build packets and send them.
- RX: sniff packets and read parsed metadata.

## Include Files

Use these headers from application code:

```c
#include "pf.h"
#include "packet_builder.h"
#include "tx.h"
```

`pf.h` contains driver setup, sniffing, capture mode, and network discovery APIs.

`packet_builder.h` contains Scapy-like packet builders.

`tx.h` contains `pf_tx_raw()`, the raw TX entry point.

## TX Packet Builders

Packet builders create normal 802.11 frame bytes.

They return a `pf_frame_t`:

```c
typedef struct {
    uint8_t bytes[PF_TX_MAX_FRAME_LEN];
    uint32_t len;
    bool ok;
} pf_frame_t;
```

Use it like this:

```c
pf_frame_t packet = Beacon("HELLO WORLD", mac, 6, NULL);

if (packet.ok) {
    pf_tx_raw(packet.bytes, packet.len);
}
```

`bytes` is the 802.11 frame.

`len` is the frame length.

`ok` tells you whether the builder succeeded.

The frame does not include FCS. The driver/TX path handles the hardware side.

## Supported TX Builders

| Builder | Creates | Typical Use |
| --- | --- | --- |
| `Beacon()` | Beacon frame | announce a lab SSID |
| `ProbeRequest()` | Probe Request frame | ask for an SSID |
| `ProbeResponse()` | Probe Response frame | answer a probe |
| `Deauth()` | Deauthentication frame | lab MAC-management experiments |

Use deauthentication frames only in controlled lab environments and on devices you own or are explicitly allowed to test.

## Get A MAC Address

Most packet builders need a sender MAC.

Use this when you want the real ESP Wi-Fi STA MAC:

```c
uint8_t mac[6];

if (!pf_get_esp_wifi_mac(mac)) {
    return;
}
```

Use this when you want a MAC derived from the ESP but marked as locally administered and unicast:

```c
uint8_t mac[6];

if (!pf_get_esp_packet_mac(mac)) {
    return;
}
```

For most generated management frames, `pf_get_esp_packet_mac()` is the better default.

## Beacon

Create a Beacon:

```c
uint8_t mac[6];
pf_frame_t beacon;

pf_get_esp_packet_mac(mac);

beacon = Beacon("LAB OPEN", mac, 6, NULL);
if (beacon.ok) {
    pf_tx_raw(beacon.bytes, beacon.len);
}
```

Function:

```c
pf_frame_t Beacon(const char *ssid,
                  const uint8_t sender_mac[6],
                  uint8_t channel,
                  const pf_beacon_options_t *options);
```

Default fields:

| Field | Default |
| --- | --- |
| destination | broadcast |
| sender | `sender_mac` |
| BSSID | `sender_mac` |
| sequence | `0` |
| beacon interval | `100` |
| capability | ESS/open |
| country | `ES ` |

Options:

```c
typedef struct {
    const uint8_t *destination;
    const uint8_t *bssid;
    uint16_t sequence;
    uint16_t beacon_interval;
    uint16_t capability_info;
    const char *country;
} pf_beacon_options_t;
```

Example with options:

```c
pf_beacon_options_t options = {
    .beacon_interval = 100,
    .country = "ES ",
};

pf_frame_t beacon = Beacon("LAB OPEN", mac, 6, &options);
```

## Probe Request

Create a Probe Request:

```c
uint8_t sta_mac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
pf_frame_t probe;

probe = ProbeRequest("LAB OPEN", sta_mac, 6, NULL);
if (probe.ok) {
    pf_tx_raw(probe.bytes, probe.len);
}
```

Function:

```c
pf_frame_t ProbeRequest(const char *ssid,
                        const uint8_t sender_mac[6],
                        uint8_t channel,
                        const pf_probe_request_options_t *options);
```

Default fields:

| Field | Default |
| --- | --- |
| destination | broadcast |
| sender | `sender_mac` |
| BSSID | broadcast |
| sequence | `0` |

Options:

```c
typedef struct {
    const uint8_t *destination;
    const uint8_t *bssid;
    uint16_t sequence;
} pf_probe_request_options_t;
```

## Probe Response

Create a Probe Response:

```c
uint8_t ap_mac[6];
uint8_t sta_mac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
pf_probe_response_options_t options;
pf_frame_t response;

pf_get_esp_packet_mac(ap_mac);

options = (pf_probe_response_options_t){
    .destination = sta_mac,
};

response = ProbeResponse("LAB OPEN", ap_mac, 6, &options);
if (response.ok) {
    pf_tx_raw(response.bytes, response.len);
}
```

Function:

```c
pf_frame_t ProbeResponse(const char *ssid,
                         const uint8_t sender_mac[6],
                         uint8_t channel,
                         const pf_probe_response_options_t *options);
```

Default fields:

| Field | Default |
| --- | --- |
| destination | broadcast |
| sender | `sender_mac` |
| BSSID | `sender_mac` |
| sequence | `0` |
| beacon interval | `100` |
| capability | ESS/open |

Options:

```c
typedef struct {
    const uint8_t *destination;
    const uint8_t *bssid;
    uint16_t sequence;
    uint16_t beacon_interval;
    uint16_t capability_info;
} pf_probe_response_options_t;
```

## Deauth

Create a Deauthentication frame:

```c
uint8_t ap_mac[6];
uint8_t sta_mac[6] = {0x02, 0xaa, 0xbb, 0xcc, 0xdd, 0xee};
pf_deauth_options_t options;
pf_frame_t deauth;

pf_get_esp_packet_mac(ap_mac);

options = (pf_deauth_options_t){
    .destination = sta_mac,
    .reason_code = 1,
};

deauth = Deauth(ap_mac, 6, &options);
if (deauth.ok) {
    pf_tx_raw(deauth.bytes, deauth.len);
}
```

Function:

```c
pf_frame_t Deauth(const uint8_t sender_mac[6],
                  uint8_t channel,
                  const pf_deauth_options_t *options);
```

Default fields:

| Field | Default |
| --- | --- |
| destination | broadcast |
| sender | `sender_mac` |
| BSSID | `sender_mac` |
| sequence | `0` |
| reason code | `1` |

Options:

```c
typedef struct {
    const uint8_t *destination;
    const uint8_t *bssid;
    uint16_t sequence;
    uint16_t reason_code;
} pf_deauth_options_t;
```

## Sequence Numbers

802.11 management frames have a 12-bit sequence number.

You can set it when building:

```c
pf_beacon_options_t options = {
    .sequence = 10,
};

pf_frame_t beacon = Beacon("LAB OPEN", mac, 6, &options);
```

Or update an existing frame:

```c
pf_frame_set_sequence(&beacon, sequence++);
```

This updates the Sequence Control field and keeps fragment number zero.

## Builder Limits

Current limits:

- channel must be 1-13;
- SSID is clamped to 32 bytes;
- frame must fit in `PF_TX_MAX_FRAME_LEN`;
- only management frame builders exist right now;
- builders do not append FCS;
- builders do not configure rate, retries, ACK policy, or PLCP fields.

## Raw TX

`pf_tx_raw()` sends a complete 802.11 frame buffer:

```c
bool pf_tx_raw(const uint8_t *frame, uint32_t len);
```

Example:

```c
if (!pf_tx_raw(packet.bytes, packet.len)) {
    /* TX failed */
}
```

The current backend is a native experimental ESP32-C3 TX path. It uses observed MMIO slots, DMA descriptors, PLCP registers, and completion/error polling.

It does not call `esp_wifi_80211_tx()`.

## RX Sniffing

Start the driver and sniff on one channel:

```c
pf_init();
pf_sniff(6);
```

Use `pf_sniff(0)` to request channel hopping:

```c
pf_sniff(0);
```

Stop sniffing:

```c
pf_sniff_stop();
```

## Reading Captured Packets

The RX task writes packet history while your application runs.

Read metadata copies instead of touching the live history directly:

```c
pf_packet_metadata_t packet;
size_t count = pf_packet_history_count();

for (size_t i = 0; i < count; i++) {
    if (!pf_packet_history_get_metadata(i, &packet)) {
        continue;
    }

    if (packet.type == PF_PACKET_TYPE_MANAGEMENT) {
        /* packet.ssid, packet.rssi, packet.channel, etc. */
    }
}
```

Use `pf_packet_history_get_copy()` only when you really need the retained raw bytes.

## Capture Modes

Choose how much data the driver stores for each received packet.

| Mode | Meaning |
| --- | --- |
| `PF_CAPTURE_MODE_METADATA_ONLY` | store parsed metadata only |
| `PF_CAPTURE_MODE_SMART` | store raw bytes only when your selector asks for them |
| `PF_CAPTURE_MODE_FULL` | store raw bytes for every captured packet |

Smart capture example:

```c
static bool keep_eapol(const pf_packet_t *packet, void *ctx)
{
    (void)ctx;
    return packet->type == PF_PACKET_TYPE_DATA && packet->is_eapol;
}

pf_set_capture_mode(PF_CAPTURE_MODE_SMART);
pf_set_capture_selector(keep_eapol, NULL);
```

## Packet Metadata

Parsed packets can expose:

- RSSI;
- channel;
- frame control;
- 802.11 type and subtype;
- address fields;
- SSID for supported management frames;
- LLC ethertype for supported data frames;
- EAPOL classification;
- optional raw bytes.

## Network Discovery

Passive network inventory is built from Beacon and Probe Response frames.

```c
size_t count = pf_network_count();
pf_network_t network;

for (size_t i = 0; i < count; i++) {
    if (pf_network_get_copy(i, &network)) {
        /* inspect network.ssid, network.bssid, network.channel, network.rssi */
    }
}
```

`pf_connect(ssid, password)` does not complete a full Wi-Fi connection yet.

Today it prepares a connection target:

- scans for the SSID;
- selects the strongest matching network;
- records BSSID, channel, security, AKM, and ciphers;
- fixes sniffing to the target channel.

## API Cheat Sheet

Driver:

```c
bool pf_init(void);
void pf_debug(bool enabled);
bool pf_set_channel(uint8_t primary, pf_secondary_channel_t secondary);
const pf_packet_list_t *pf_sniff(uint8_t primary_channel);
void pf_sniff_stop(void);
```

TX:

```c
pf_frame_t Beacon(...);
pf_frame_t ProbeRequest(...);
pf_frame_t ProbeResponse(...);
pf_frame_t Deauth(...);
bool pf_tx_raw(const uint8_t *frame, uint32_t len);
```

RX history:

```c
size_t pf_packet_history_count(void);
bool pf_packet_history_get_metadata(size_t index, pf_packet_metadata_t *out);
bool pf_packet_history_get_copy(size_t index, pf_packet_t *out);
```

Networks:

```c
size_t pf_network_count(void);
bool pf_network_get_copy(size_t index, pf_network_t *out);
bool pf_connect(const char *ssid, const char *password);
```
