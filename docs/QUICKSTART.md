# Quickstart

This guide shows the shortest path from "I opened the repo" to "I can send and inspect packets".

## What This Project Gives You

The Packetfather is an experimental ESP32-C3 Wi-Fi packet stack.

Right now it can:

- initialize the custom Wi-Fi driver path;
- sniff packets into parsed packet objects;
- build a few 802.11 management frames;
- transmit raw 802.11 frames with the native experimental TX path.

The current packet builders are:

- `Beacon()`
- `ProbeRequest()`
- `ProbeResponse()`
- `Deauth()`

## Minimal TX Example

This creates a Beacon frame and sends it:

```c
#include "packet_builder.h"
#include "pf.h"
#include "tx.h"

void app_main(void)
{
    uint8_t mac[6];
    pf_frame_t beacon;

    if (!pf_init()) {
        return;
    }

    pf_set_channel(6, PF_SECONDARY_CHANNEL_NONE);

    if (!pf_get_esp_packet_mac(mac)) {
        return;
    }

    beacon = Beacon("HELLO WORLD", mac, 6, NULL);
    if (beacon.ok) {
        pf_tx_raw(beacon.bytes, beacon.len);
    }
}
```

## Sending In A Loop

Wi-Fi management frames normally need a changing sequence number.

```c
uint16_t sequence = 0;

while (1) {
    pf_frame_set_sequence(&beacon, sequence++);
    pf_tx_raw(beacon.bytes, beacon.len);
    vTaskDelay(pdMS_TO_TICKS(100));
}
```

## Seeing Frames In Wireshark

Use a monitor-mode Wi-Fi adapter on the same channel as the ESP32-C3.

For the default example in `main.c`:

- channel: `6`
- SSID: `HELLO WORLD`

Useful Wireshark filters:

```text
wlan.ssid == "HELLO WORLD"
```

```text
wlan.sa == 02:11:22:33:44:55
```

```text
wlan.fc.type_subtype == 0x08
```

`0x08` is Beacon.

## Minimal RX Example

This starts sniffing on channel 6 and reads parsed metadata:

```c
pf_packet_metadata_t packet;
size_t count;

pf_init();
pf_sniff(6);

count = pf_packet_history_count();

for (size_t i = 0; i < count; i++) {
    if (!pf_packet_history_get_metadata(i, &packet)) {
        continue;
    }

    if (packet.type == PF_PACKET_TYPE_MANAGEMENT) {
        /* inspect packet.ssid, packet.rssi, packet.channel, etc. */
    }
}
```

## The Mental Model

For TX:

```text
Beacon()/ProbeRequest()/... -> pf_frame_t -> pf_tx_raw() -> native TX backend
```

For RX:

```text
Wi-Fi DMA -> parser -> pf_packet_t history -> metadata copies
```

## Current Limits

- ESP32-C3 is the current target.
- TX is experimental.
- Packet builders currently cover out-of-network management frames only.
- Builders do not append FCS.
- Builders do not choose TX rate or retries.
- RX delivery is currently history/polling based, not callback based.

## Next File To Read

Read [PACKET_API.md](PACKET_API.md) for the full packet API.
