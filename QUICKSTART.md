# Quick start

Whiskey Fox Digi replaces the original PK-88 ROM with a standalone APRS digipeater.
Build the image, program it into a 27C256, install it in the TNC, and talk to the TNC over the serial port at 9600 baud.
[COMMANDS](COMMANDS.md) is the command reference. Check [LEDS](LEDS.md) for the meaning
of each of the front panel LEDs when running the WFDIGI firmware.

It is possible to use the generic pre-build binary, but this relies on the battery
backed SRAM for all of the digipeater configuration settings which you configure
via the serial console command line interface. 
It is also possible, and encouraged, to burn a custom build of WFDIGI with your 
desired settings baked in as the cold boot defaults so if your digipeater ever
loses its SRAM for some reason it can come back up in a working state.
This can be done by editing the `/include/config.h` file and running `make`.

## Build tools

The firmware is written in C and built with SDCC.
`sdcc`, `sdasz80`, and `makebin` must
be on `PATH`, along with `make` and `python3`. 
The layout check uses Python to check the resulting binary and inject the checksum.
`gcc` is only needed for `make test`, which is not required to produce the
PROM image.

## Cold-boot defaults

When the PK-88 is powered on for the first time or after removing J6 (or after
a power cycle when the backup battery is dead) you will see it print `Cold boot...`
to the terminal as it is copying the ROM default settings into RAM.
Subsequent power cycles where a valid config is detected will instead print
`Warm boot...` to the terminal, indicating that an existing config has been picked
up and any prior config changes have been retained.

You can always back up your current rnning config by running `DISPLAY` and saving
the output to copy-paste back into the terminal to re-apply.
If you want to blow away all changes made to the configuration, either power off
the TNC and remove the `J6` jumper to disconnect the backup battery, or
run the `RESET` command to wipe all of the settings and perform a cold boot.

## Program the PK-88

Burn `build/wfdigi.bin` onto a 27C256 EPROM. Install that PROM in the PK-88
in place of the original ROM. Connect a terminal at 9600 baud, 8 data bits, no
parity, and one stop bit, with RTS/CTS hardware flow control. There is no
autobaud detection like the original PK-88 firmware.

Power the TNC on. The front-panel lamps walk once to show they all work and you're
running WFDIGI firmware and the terminal shows the banner and a callsign prompt.

```
Whiskey Fox Digi - version <git-describe>
Copyright 2026 - Kenneth Finnegan
Cold boot...
N0CALL>
```

## Set the station up

The shipped callsign is `N0CALL`. The transmitter will not key while that
callsign is still set, and the CMD lamp blinks at 2 Hz indicating there is
a transmitter interlock active preventing us from keying the radio.
A frame that would have gone out is discarded, and the TNC prints 
`ERR - Set Callsign`.
Digipeating and beacons stay idle until `MYCALL` is changed.
CMD then stays lit.

At the prompt, set the callsign, position, beacon text, and beacon interval.
`NNALIAS0` already defaults to `WIDE`, but any additional NNALIASes or ALIASes
should be configued if so desired.

```
MYCALL EXAMPL-7
MYLOC 37 23.45 N 122 01.23 W
BTEXT Whiskey Fox Digi
BEACON 10
```

Settings are applied and saved immediately, so there is no need to explicitly
save or apply configuration changes at the terminal.

To calibrate the TNC transmit audio levels, use the `CAL [L|H|D] NUMBER` command
to either send the high tone, the low tone, or both for a set number of seconds
while adjusting the potentiometer on the TNC circuit board.

`BEACON 10` queues a position beacon every 10 minutes. `BEACON OFF` disables
it. `BPATH` is a space delimited list of one to four paths to alternately use
for beacons, and a reasonable choice would be `BPATH WIDE1-1 - -` to use one
hop every third beacon.
`DIGIPEAT` and `TELEMETRY` are enabled by default. With telemetry on, a
report goes out every 10 minutes reporting on the local traffic levels seen by
the digipeater.
`TELPATH` configures the AX.25 path for telemetry packets separately from 
`BPATH` and defaults to direct (`-`), so those frames do not request a digipeater.

`VISCOUS X Y` is an optional delayed-repeat mode for fill-in digipeaters. 
It enables `DIRECTONLY`, waits a random X-Y seconds before repeating a
direct packet, and suppresses that repeat if another digipeater's copy
is heard first. `VISCOUS OFF`
restores immediate digipeating and turns `DIRECTONLY` off. `DIRECTONLY OFF`
turns `VISCOUS` off as well. A reasonable choice for a fill-in digipeater is
`VISCOUS 3 6`.

`DISPLAY` prints the stored settings. If you ever want to fully back up your
digipeater settings, save the output of `DISPLAY` and paste it back into the
digipeater to restore.
Note that pasting an entire configuration into WFDIGI relies heavily on hardware
flow control since the Z80 cannot keep up with input at 9600 baud.
