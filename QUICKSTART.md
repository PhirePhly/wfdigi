# Quick start

Whiskey Fox Digi replaces the original PK-88 ROM with a standalone APRS digipeater.
Build the image, program it into a 27C256, install it in the TNC, and talk to the TNC over the serial port at 9600 baud.
[COMMANDS.md](COMMANDS.md) is the command reference.
[HARDWARE.md](HARDWARE.md) is the board reference.

It is possible to use the generic pre-build binary, but this relys on the battery
backed SRAM for all of the digipeater configuration settings which you configure
via the serial console command line interface. 
It is also possible, and encouraged, to burn a custom build of WFDIGI with your 
desired settings baked in as the cold boot defaults so if your digipeater ever
loses its SRAM for some reason it can come back up in a working state.

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
not match. `RESET` zeros the stored image and reboots, which prints
`Cold boot...` and loads these defaults. `REBOOT` starts over and keeps the
settings already stored in battery SRAM, which prints `Warm boot...`.

## Program the PK-88

Program `build/wfdigi.bin` into a 27C256 EPROM. Install that PROM in the PK-88
in place of the original ROM. Connect a terminal at 9600 baud, 8 data bits, no
parity, and one stop bit, with RTS/CTS hardware flow control. There is no
autobaud detection.

Power the TNC on. The front-panel lamps walk once to show they all work and you're
running WFDIGI firmware, then the terminal shows the banner and a callsign prompt.
The version is `git describe --tags --always --dirty` from the build:

```
Whiskey Fox Digi - version <git-describe>
Copyright 2026 - Kenneth Finnegan
Cold boot...
N0CALL>
```

## Set the station up

The shipped callsign is `N0CALL`. The transmitter will not key while that
callsign is still set, and the CMD lamp blinks at 2 Hz indicating there is
a transmitter interlock enabled preventing us from keying the radio.
A frame that would have gone out is discarded, and the TNC prints 
`ERR - Set Callsign`.
Digipeating and beacons stay idle until `MYCALL` is changed.
CMD then stays lit.

At the prompt, set the callsign, position, beacon text, and beacon interval.
`NNALIAS0` already defaults to `WIDE`, but any additional NNALIASes or ALIASes
should be configued if so desired.

```
MYCALL W6FOO
MYLOC 37 23.45 N 122 01.23 W
BTEXT Whiskey Fox Digi
BEACON 10
```

`BEACON 10` queues a position beacon every 10 minutes. `BEACON OFF` disables
it. `WIDE` matches `WIDE2-2`, `WIDE1-1`, and the other n-N forms of that
prefix. `DIGIPEAT` and `TELEMETRY` are already on. With telemetry on, a
report goes out every 10 minutes, and one definition message goes out each
hour.
`TELPATH` configures the AX.25 path for telemetry packets separately from 
`BPATH` and defaults to direct, so those frames do not request a digipeater.
The first definition message is the `BITS` message, sent once `MYCALL` is set.

`DISPLAY` prints the stored settings. If you ever want to fully back up your
digipeater settings, save the output of `DISPLAY` and paste it back into the
digipeater to restore.
Note that pasting an entire configuration into WFDIGI relies heavily on hardware
flow control since the Z80 cannot keep up with input at 9600 baud.
