# Whiskey Fox Digi 🥃🦊📻

Standalone feature-rich APRS digipeater firmware for the AEA PK-88 and
UIDIGI-compatible TAPR TNC-2 packet TNCs. It is a clean rewrite separate from
the OEM firmware.

It being a stand-alone digipeater means that this software is intended to be run
on a TNC without a supporting computer and strictly only support digipeating.
Default settings are able to be baked into a custom firmware build to protect
against an SRAM battery backup failure, but all digipeater settings are possible
to be modified via the local serial terminal on the TNC.

Installation is by replacing the TNC's 27C256 firmware ROM with the matching
WFDIGI platform image (IC15 on the PK-88).
It is likely that 29C256 FLASH ROMs would be usable as replacements if one
lacked a UV ROM eraser, but this has been untested and unqualified.
At boot the PK-88 scans all eight controllable lamps. The TNC-2 tests its two
software-controlled lamps. The PK-88 `CMD` lamp then blinks if the transmitter
interlock prevents transmission.

## Feature Highlights

* Digipeater support for up to 4 n-N aliases as well as 4 fixed aliases, both with trace-style callsign substitution, so both WIDE and regional SSn-N aliases are supported equally.

* Preemptive digipeating to respond to aliases other than the first available hop

* Proportional pathing so location beacons alternate between 1-4 different path settings

* Max Hop policy enforcement so abusive WIDE7-7 paths are quashed but still digipeated once

* Direct-Only alias digipeating so as a fill-in digipeater we only digipeat packets with zero used VIA hops reagrdless of what alias users specify in their path. This relieves us from relying on every end user correctly using paths like `WIDE1-1,WIDE2-1` to enable low-level fill-in digipeaters.

* Viscous digipeating, where digipeated packets are held in escrow for a random number of seconds to see if another digipeater also heard the packet and discard packets which have successfully been digipeated by another station to avoid redundant traffic.

* Telemetry support to enable public visibility for the traffic levels in and out of the digipeater.

* APRS messages addressed to `MYCALL` are acknowledged.
General `?APRS?` queries, directed `?APRS?` capability queries, `?APRSD`
heard-direct queries, `?APRSP` position queries, `?APRSU` telemetry definition
queries, and `?APRSV` version queries are supported.

* All user-facing settings support defaults being baked into the firmware ROM for protection from CMOS battery failure.

## Hardware targets

The `pk88` target supports an unmodified AEA PK-88:

- Z80 at 4.9152 MHz
- 32 KiB EPROM at `0x0000` and 32 KiB battery-backed SRAM at `0x8000`
- Z8530 channel A (HDLC, 7910 Bell 202 modem) at `0xF2`/`0xF3`
- Z8530 channel B (asynchronous RS-232 terminal) at `0xF0`/`0xF1`
- Front-panel LED latch at `0xF4`, TX watchdog pet at `0xF8`

The `tnc2` target supports the UIDIGI-compatible TNC-2 layout:

- Z80 SIO/0 channel A (HDLC radio) at `0xDC`/`0xDD`
- Z80 SIO/0 channel B (asynchronous terminal) at `0xDE`/`0xDF`
- External radio and terminal clocks, 32 KiB ROM and 32 KiB RAM
- Channel-B sync transitions provide a 600 Hz timer interrupt
- No software-serviced TX watchdog
- The STA output lights after a successfully decoded packet
- The CON output lights while a packet is in the viscous digipeater queue

The PK-88 terminal port is fixed at 9600 baud, 8 data bits, no parity, and one stop
bit. There is no autobaud detection. The radio channel is initialized for
1200 baud AX.25: HDLC, NRZI, and equalized Bell 202. `EQUALIZER OFF` selects Bell 202
without the equalization receive filter enabled, which may improve receive performance
under some conditions.

## Building

`sdcc`, `sdasz80`, and `makebin` must be in your `PATH`.

```sh
make
```

The default `make` builds and validates both platforms. `make pk88` and
`make tnc2` build one platform. Final products are placed in `build/`:

- `wfdigi-pk88.bin` — PK-88 32 KiB ROM image
- `wfdigi-tnc2.bin` — TNC-2 32 KiB ROM image
- `pk88/` and `tnc2/` — per-platform Intel HEX, map, and object files

The final two ROM bytes hold a CRC-16 over `0x0000`–`0x7FFD`. `make` rejects a
ROM that runs into RAM at `0x8000`, a reset vector other than
`DI` / `LD SP,0`, an incomplete mode-2 vector table, or a baud-rate constant
or hardware signature inconsistent with the selected platform.

`make test` compiles the digipeater, packet queue, duplicate list, heard
list, and messaging on the host and checks alias, n-N, `DIRECTONLY`, `VISCOUS`,
`MAXHOPS`, duplicate suppression, message acknowledgements, and APRS queries.
It does not need SDCC or a Z80 to run the tests.

## Firmware organization

- `platform/pk88/`, `platform/tnc2/`: target startup, controller setup, I/O,
  lamps, PTT, watchdog behavior, and TNCID
- `src/isr_stubs.s`: shared interrupt entry/exit stubs
- `src/lamps.c`: which front-panel lamps are lit on each service pass
- `src/serial.c`: terminal I/O, with a receive interrupt and RTS flow control
- `src/pktq.c`: transmit and viscous queues sharing a pool of 31-byte blocks
- `src/modem.c`: HDLC AX.25 receive and transmit on the radio channel
- `src/digi.c`: path matching, header rewrite, and the transmit queue for repeats
- `src/messaging.c`: APRS messages and queries addressed to `MYCALL`, plus general `?APRS?`
- `src/dupe.c`: the last 250 transmitted packets, used to skip a repeat
- `src/heard.c`: the last 50 stations heard directly, for `?APRSD`
- `src/util.c`: shared helper functions
- `src/beacon.c`: APRS position beacon
- `src/telemetry.c`: 10-minute packet counts and telemetry definitions every four hours
- `src/timer.c`: shared 10 ms countdown timers, divided from each platform's
  channel-B timer interrupt rate
- `src/cli.c`: callsign line editor. SSID 0 is omitted from the prompt
- `include/config.h`: cold-boot defaults. Edit this file and run `make`
- `src/config.c`: range checks and the SRAM image at `0x8000`
- `src/main.c`: boot banner, lamp test, and the service loop
- `src/interrupts.c`: mode-2 handlers. The radio channel interrupts for HDLC
  receive and transmit. The terminal channel interrupts on each received
  character and on each `/SYNCB` edge

## What the current ROM does

1. Program `build/wfdigi-pk88.bin` or `build/wfdigi-tnc2.bin` into a 32 KiB
   EPROM and cold-reset the matching TNC.
2. A terminal set to 9600 8N1 receives:

    ```
    Whiskey Fox Digi - version <git-describe> <platform>
    Copyright 2026 - Kenneth Finnegan
    ROM checksum: PASS
    Cold boot...
    ```

   `<git-describe>` is the `git describe --tags --always --dirty` string from the
   tree that built the ROM. The same text is the `wfdigi_version` string.
   `<platform>` is `pk88` or `tnc2`, the `wfdigi_platform` string.
   The checksum is the CRC-16 of `0x0000` through `0x7FFD`. A mismatch prints
   `ROM checksum: FAIL` and the firmware still boots.

   A later reset with a matching configuration CRC prints `Warm boot...` instead
   and keeps the settings entered at the prompt.

3. Cold boot copies the defaults from `include/config.h` into the battery SRAM image at `0x8000`.
   Each value is range-checked as it is stored. A value outside its limits is
   left clear and reported as `Bad config: NAME`. The image is marked valid
   only when every parameter passes. A CRC-16 is then stored in the next two
   bytes. A later warm boot checks that CRC, keeps the config, and still restarts the
   timers, modem, packet queue, duplicate list, drop counters, and received and transmitted frame counts from zero.
4. On the PK-88, the eight front-panel lamps scan once to indicate you are no
   longer running stock firmware. The TNC-2 tests its two controllable outputs.
   PK-88 CMD then stays lit, unless `MYCALL`
   is still `N0CALL`, in which case CMD blinks at 2 Hz until the callsign is
   changed to unlock the transmitter interlock prevent `N0CALL` on the air.
   The DCD lamp indicates the channel receivier is busy.
   STA lights for 400 ms after each valid received frame.
   MULT lights while multiple packets are waiting in the transmit queue.
   CON lights while a frame is waiting in the viscous queue.

Cold-boot defaults:

| Parameter | Default | Accepted range |
|---|---|---|
| MYCALL | `N0CALL-0` | AX.25 call, SSID 0–15 |
| DIGIPEAT | on | off or on |
| DIRECTONLY | off | off or on |
| DROPLIST | `-` | 0–10 calls; `-SSID` or `-*` for every SSID; `-` clears the list |
| VISCOUS | off | off, or minimum and maximum seconds from 1–9 |
| LOGGING | on | off or on |
| TELEMETRY | on | off or on |
| TELPATH | `-` | one callsign, or `-` for no path |
| TXDELAY | 30 (300 ms) | 0–120, in 10 ms steps |
| PPERSIST | 63 | 0–255 |
| SLOTTIME | 10 (100 ms) | 0–255, in 10 ms steps |
| TXSTALE | 10 | 0–255 seconds; 0 or OFF disables the stale timeout, silent in DISPLAY when 10 |
| FULLDUPLEX | off | off or on |
| EQUALIZER | on | off or on |
| ALIAS 0–3 | blank, disabled | an AX.25 call and SSID, or empty to disable that slot |
| NNALIAS 0–3 | `WIDE`, then three blank slots | an n-N prefix, or empty to disable that slot |
| BEACON | off | off, 1–60 minutes |
| BTEXT | empty | printable ASCII, NUL terminated |
| BPATH | `-` | 1–4 paths; a callsign, or `-` for no path |
| MYLOC | `00 00.00 N 000 00.00 E` | 0-90 0.0-59.99 [NS] 0-180 0.0-59.99 [EW] |
| MAXHOPS | 3 | 1-8 |
| MYSYMBOL | `/#` | two characters: primary `/`, alternate `\`, or overlay `0-9`/`A-Z`, then a symbol code |

Every transmission ends with 3 HDLC flags. The radio is unkeyed when nothing
else is waiting. A frame already in the transmit queue follows those flags
immediately, and `TXDELAY` is used only when the radio first keys up. Half duplex
waits until the channel is clear, then keys when a draw from 0 to 255 is
less than or equal to `PPERSIST`. Otherwise it waits one `SLOTTIME` and
draws again. Full duplex keys without that wait. A frame sent while the
radio stays keyed does not draw again.

At the callsign `>` prompt, a config name alone prints the value stored in SRAM.
`NAME VALUE` updates that value when it is in range. `DISPLAY` prints every
setting and is an effective way to take a backup of the full digipeater config.
`HELP` prints where to read the documentation. `ENGSTAT` prints
low level diagnostics counters of the wfdigi packet engine. `SENDRAW` queues
one UI frame from `MYCALL` to the platform TNCID (`APWFDP` on PK-88 or
`APWFD2` on TNC-2), via the first `BPATH` entry when that
entry is a callsign.
`REBOOT`
starts the firmware over and keeps the stored settings. `RESET` clears the
stored settings and reboots, so the boot loads the defaults. Unknown commands
print `Huh?`; lines longer than 127 characters print
`Too long?`. [COMMANDS.md](COMMANDS.md) describes the line editor and the
meaning of each command.

An n-N prefix is at most five characters, so the hop-limit digit still fits in
the six-character AX.25 callsign. `WIDE` matches `WIDE2-2` and `WIDE2-1`: the
callsign is the prefix plus a digit N from 1 to 7, and the SSID is the
remaining hop count n, with n from 1 through N. It does not match a bare
`WIDE`. An SSID above n, such as `WIDE1-2`, is quashed without taking a hop,
so it goes out as `WIDE1-2*,MYCALL*` even when the request is within `MAXHOPS`.

A digipeat searches the whole path, not only the first unused address. `MYCALL`
is taken first and every hop through it is marked repeated. A path in which
`MYCALL` is already repeated has looped and is not sent again. An alias is
replaced by `MYCALL`. An n-N address is replaced by `MYCALL`, and the same
n-N call is appended at the end with the SSID reduced by one when that SSID
is still at least 1. `WIDE2-2` goes out as `MYCALL*,WIDE2-1`. `WIDE2-1` goes
out as `MYCALL*`. `DIRECTONLY` limits alias and n-N repeats to a path whose
first hop has not been used yet.
A path that already has `MAXHOPS` repeated digipeaters
is not repeated. An unused address counts as one hop unless it matches a
configured n-N prefix, in which case it counts as the remaining hop count. A
used digipeater counts as one hop. The hop total is those used hops plus the
remaining request. A total equal to `MAXHOPS` is repeated normally. A larger
total is quashed: the matched n-N is decremented by one, every digipeater is
marked used, and `MYCALL` is appended so the last repeated hop is this station.
An unused n-N with the SSID above N is quashed the same way, except that hop
count is left unchanged.
One used hop followed by `WIDE2-2` therefore goes out as `AAA,WIDE2-1*,MYCALL*`
when `MAXHOPS` is 2. When the path already
holds eight digipeaters, `MYCALL` replaces the last one. A path with no
matching address is left alone.

An APRS message whose addressee is `MYCALL` is answered and is not digipeated.
The addressee is nine characters, SSID 0 is omitted, and letter case does not
matter. A numbered message, whose text ends in `{` and one to five letters or
digits, is acknowledged with `ack` and that number before any other reply.
The reply is a UI frame from `MYCALL` to the platform TNCID, via the first `BPATH`
entry when that entry is a callsign, and it does not advance the beacon path.
A directed `?APRS?` answers with `?APRSD ?APRSP ?APRSU ?APRSV`.
`?APRSD` answers with one or more `Directs=` messages listing the stations
heard directly in the last eight hours, five per message.
`?APRSP` queues the normal position beacon, at most once every 30 seconds.
`?APRSU` queues the four telemetry definition messages at once.
`?APRSV` answers with the firmware version and platform, such as
`v0.2 pk88`. Any other
`?APRS` letter is answered with `ERR - query type unsupported`. A general
`?APRS?` may queue that same beacon and is then digipeated. A third-party
frame begins with `}` and uses the callsign before `>` as the station those
rules apply to. [COMMANDS.md](COMMANDS.md) describes the replies, the cooldown,
and which frames are still digipeated.

`VISCOUS X Y` enables `DIRECTONLY` and holds digipeated packets for a
random `X` through `Y` seconds. A copy heard through another digipeater during
that delay suppresses the queued repeat. Otherwise the frame moves to the
transmit queue when its delay expires. A path containing this station's
specific `MYCALL` is sent immediately instead of entering the viscous queue.
Viscous digipeating is purely a first hop fill-in digipeater concept, so
`DIRECTONLY` is required for viscous digipeating to be enabled. When enabled,
a viscous digipeater very effectively increases low-level digipeater coverage
while not increasing congestion when it is observed that higher level / other
digipeaters have already successfully picked up the same packet.
