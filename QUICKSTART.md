# Quick start

Whiskey Fox Digi replaces the original PK-88 ROM with a standalone APRS digipeater.
Build the image, program it into a 27C256, install it in the TNC, and talk to the TNC over the serial port at 9600 baud.
[COMMANDS.md](COMMANDS.md) is the command reference.
[HARDWARE.md](HARDWARE.md) is the board reference.

## Build tools

The firmware is built with SDCC 4.6.0. `sdcc`, `sdasz80`, and `makebin` must
be on `PATH`, along with `make` and `python3`. The layout check uses Python.
`gcc` is only needed for `make test`, which is not required to produce the
PROM image.

Install [SDCC 4.6.0](https://sdcc.sourceforge.net/) and confirm the three
tools before building:

```sh
sdcc -v
sdasz80 -v
makebin
```

## Cold-boot defaults

`include/config.h` holds the settings copied into battery SRAM on a cold boot.
Edit that file, then rebuild. A callsign is up to six characters, `A`–`Z` and
`0`–`9`, with the SSID in the matching `*_SSID` macro. An empty alias or n-N
prefix disables that slot. `CFG_BEACON` is the interval in minutes, and `0`
leaves the beacon off. `CFG_MYLOC` is degrees and decimal minutes, for
example `"37 23.45 N 122 01.23 W"`.

```sh
make
```

The PROM file is `build/wfdigi.bin`, a 32 KiB image with unused bytes filled
with `0xFF`.

Those defaults are used when the TNC prints `Cold boot...`. That is the first
time this firmware runs, and any later reset whose configuration checksum does
not match. A reset that prints `Warm boot...` keeps the settings already
stored in battery SRAM.

## Program the PK-88

Program `build/wfdigi.bin` into a 27C256 EPROM. Install that PROM in the PK-88
in place of the original ROM. Connect a terminal at 9600 baud, 8 data bits, no
parity, and one stop bit. There is no autobaud detection.

Power the TNC on. The front-panel lamps walk once, then the terminal shows
the banner and a callsign prompt:

```
Whiskey Fox Digi - version 0.1
Copyright 2026 - Kenneth Finnegan
Cold boot...
N0CALL>
```

## Set the station up

The shipped callsign is `N0CALL`. The transmitter will not key while that
callsign is still set, and the CMD lamp blinks at 2 Hz. A frame that would
have gone out is discarded, and the TNC prints `ERR - Set Callsign`.
Digipeating and beacons stay idle until `MYCALL` is changed. CMD then stays
lit.

At the prompt, set the callsign, position, beacon text, beacon interval, and
one n-N prefix. The useful n-N setting is `NNALIAS0 WIDE`. Leave the plain
alias slots off.

```
MYCALL W6FOO
MYLOC 37 23.45 N 122 01.23 W
BTEXT Whiskey Fox Digi
BEACON 10
NNALIAS0 WIDE
```

`BEACON 10` queues a position beacon every 10 minutes. `BEACON OFF` disables
it. `WIDE` matches `WIDE2-2`, `WIDE1-1`, and the other n-N forms of that
prefix. `DIGIPEAT` is already on.

`DISPLAY` prints the stored settings. The next prompt uses the new callsign.
