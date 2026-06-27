# Documentation

Start here if you want to use or understand The Packetfather.

The project is experimental, but the docs are organized so you can find the useful part quickly.

## Read In This Order

1. [QUICKSTART.md](QUICKSTART.md)

   Build a first mental model and send simple packets.

2. [PACKET_API.md](PACKET_API.md)

   Use the packet API: create frames, transmit them, sniff frames, and read parsed metadata.

3. [DRIVER_OVERVIEW.md](DRIVER_OVERVIEW.md)

   Understand how the code is split into modules.

4. [STATUS.md](STATUS.md)

   See what works today and what is still experimental.

5. [ROADMAP.md](ROADMAP.md)

   See the next implementation milestones.

6. [HARDWARE_NOTES.md](HARDWARE_NOTES.md)

   Low-level ESP32-C3 register and DMA notes. You only need this when working on the driver internals.

## Most Common Tasks

| Goal | Read |
| --- | --- |
| Send a Beacon or Probe Request | [QUICKSTART.md](QUICKSTART.md), [PACKET_API.md](PACKET_API.md#tx-packet-builders) |
| Create custom 802.11 management frames | [PACKET_API.md](PACKET_API.md#tx-packet-builders) |
| Read captured packets | [PACKET_API.md](PACKET_API.md#rx-sniffing) |
| Understand project structure | [DRIVER_OVERVIEW.md](DRIVER_OVERVIEW.md) |
| Work on DMA, registers, or PHY behavior | [HARDWARE_NOTES.md](HARDWARE_NOTES.md) |

## Important Warning

This is a lab Wi-Fi driver and packet stack. Use packet injection only on hardware, networks, and devices you own or are explicitly allowed to test.
