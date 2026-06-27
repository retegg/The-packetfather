# Project Status

Current maturity: early experimental prototype.

The project is useful for lab experiments, but it is not a finished Wi-Fi driver yet.

## Works Today

| Area | Status |
| --- | --- |
| ESP32-C3 target | active target |
| custom Wi-Fi/PHY bring-up | exists |
| RX DMA setup | exists |
| packet parser | exists, still growing |
| packet history | exists |
| metadata-only capture | exists |
| smart/full capture | exists |
| passive network inventory | exists |
| raw TX API | exists |
| native TX backend | experimental, currently visible in Wireshark |
| packet builders | Beacon, Probe Request, Probe Response, Deauth |

## Not Finished Yet

| Area | Current Gap |
| --- | --- |
| full Wi-Fi connection | not implemented |
| open AP/network mode | not implemented yet |
| RX callbacks | not implemented |
| generic filters | not implemented |
| stable channel retune | still experimental |
| broad ESP32 support | ESP32-C3 only for now |
| complete Scapy-like API | first layer only |

## Practical Summary

You can currently:

- build simple 802.11 management frames;
- send them through `pf_tx_raw()`;
- see TX frames in Wireshark in lab conditions;
- sniff frames;
- read parsed metadata;
- keep a passive list of discovered networks.

You cannot yet:

- join a network with a full custom MAC implementation;
- run a complete open AP;
- handle association/authentication end to end;
- rely on this as a stable production driver.

## Current Focus

Short-term focus:

- make TX more stable;
- grow the packet builder API;
- make RX delivery easier to use;
- prepare the pieces needed for an open test network.
