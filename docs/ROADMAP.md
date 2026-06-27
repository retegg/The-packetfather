# Roadmap

This roadmap is ordered by what unlocks the next useful experiments.

## Now

Make the current TX/RX foundation easier to use.

- stabilize native TX timing and completion handling;
- keep improving packet builders;
- document packets and examples clearly;
- make Wireshark-visible tests repeatable;
- reduce malformed TX frames.

## Next

Build the APIs needed for interactive packet work.

- RX callbacks;
- programmable filters;
- cleaner packet classification;
- parser tests with captured frame fixtures;
- better raw-frame retention controls.

## Open Network Experiments

Build the first pieces of an open Wi-Fi network.

- regular Beacon scheduling;
- Probe Request detection;
- Probe Response replies;
- Authentication frame builder/parser;
- Association Request parser;
- Association Response builder;
- basic station table.

## Later

Move toward a fuller packet stack.

- more management/data/control frame builders;
- packet mutation helpers;
- custom reply logic;
- controlled retry/rate options;
- stronger channel control;
- research support for more ESP32 variants.
