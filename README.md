# Whiskey Fox Digi 🥃🦊📻

Standalone APRS digipeater firmware for the AEA PK-88 packet TNC. It is a
clean rewrite in C for the SDCC Z80 compiler, in the same spirit as UIDIGI on
the TAPR TNC-2: the board comes up as a digipeater, without the original
terminal command set.

The image in the tree today brings up the board and the radio modem. It loads
the digipeater configuration into SRAM, talks to a terminal at 9600 baud, and
sends and receives AX.25. With `BEACON` set it queues a position beacon. With
`DIGIPEAT` on, a received frame whose path matches this station is rewritten
and queued for transmit.

## Hardware target

- Z80 at 4.9152 MHz
- 32 KiB EPROM at `0x0000` and 32 KiB battery-backed SRAM at `0x8000`
- Z8530 channel A (HDLC, 7910 Bell 202 modem) at `0xF2`/`0xF3`
- Z8530 channel B (asynchronous RS-232 terminal) at `0xF0`/`0xF1`
- Front-panel LED latch at `0xF4`, watchdog pet at `0xF8`

[HARDWARE.md](HARDWARE.md) is the board reference: memory map, SCC wiring,
register values, LED bits, the 7910 mode straps, and the watchdog.

The terminal port is fixed at 9600 baud, 8 data bits, no parity, and one stop
bit. There is no autobaud detection. The radio channel is initialized for
1200 baud AX.25: HDLC, NRZI, and equalized Bell 202, with PTT held off.

## Build

`sdcc`, `sdasz80`, and `makebin` must be on `PATH`. This tree is built with
SDCC 4.6.0.

```sh
make
```

Build products are placed in `build/`:

- `wfdigi.ihx` — Intel HEX image
- `wfdigi.bin` — 32 KiB ROM image, unused bytes filled with `0xFF`
- `wfdigi.map` — linker map used by the layout check

`make` rejects a ROM that runs into RAM at `0x8000`, a reset vector other than
`DI` / `LD SP,0`, an incomplete mode-2 vector table, or a baud-rate constant
other than 9600 on the terminal and the 1200 baud DPLL clock on the radio.

```sh
make clean
```

`make test` compiles the digipeater, packet queue, and duplicate list on the host and checks
alias, n-N, `DIRECTONLY`, `VISCOUS`, `MAXHOPS`, and duplicate suppression. It does not
need SDCC.

## Firmware organization

- `src/startup.s`, `src/isr_stubs.s`: reset, the mode-2 vector table at
  `0x0100`, and the watchdog strobe
- `src/hardware.c`: SCC setup, the front-panel lamp latch, radio carrier detect, and PTT
- `src/lamps.c`: which front-panel lamps are lit on each service pass
- `src/serial.c`: terminal I/O, with a receive interrupt and RTS flow control
- `src/pktq.c`: transmit and viscous queues sharing a pool of 31-byte blocks
- `src/modem.c`: HDLC AX.25 receive and transmit on the radio channel
- `src/digi.c`: path matching, header rewrite, and the transmit queue for repeats
- `src/dupe.c`: the last 250 transmitted packets, used to skip a repeat
- `src/util.c`: shared helper functions
- `src/beacon.c`: APRS position beacon
- `src/telemetry.c`: 10-minute packet counts and the hourly telemetry definitions
- `src/timer.c`: 10 ms countdown timers, from the 1200 Hz `/SYNCB` square wave
- `src/cli.c`: callsign line editor. SSID 0 is omitted from the prompt
- `include/config.h`: cold-boot defaults. Edit this file and run `make`
- `src/config.c`: range checks and the SRAM image at `0x8000`
- `src/main.c`: boot banner, lamp test, and the service loop
- `src/interrupts.c`: mode-2 handlers. The radio channel interrupts for HDLC
  receive and transmit. The terminal channel interrupts on each received
  character and on each `/SYNCB` edge

## What the current ROM does

1. Program `build/wfdigi.bin` into a 32 KiB EPROM and cold-reset the PK-88.
2. A terminal set to 9600 8N1 receives:

    ```
    Whiskey Fox Digi - version <git-describe>
    Copyright 2026 - Kenneth Finnegan
    Cold boot...
    ```

   `<git-describe>` is the `git describe --tags --always --dirty` string from the
   tree that built the ROM. The same text is the `wfdigi_version` string.

   A later reset with a matching configuration CRC prints `Warm boot...` instead
   and keeps the settings entered at the prompt.

3. Cold boot copies the defaults from `include/config.h` into the battery SRAM image at `0x8000`.
   Each value is range-checked as it is stored. A value outside its limits is
   left clear and reported as `Bad config: NAME`. The image is marked valid
   only when every parameter passes. A CRC-16 is then stored in the next two
   bytes. Warm boot checks that CRC, keeps the image, and still starts the
   timers, modem, packet queue, duplicate list, drop counters, and received and transmitted frame counts from zero. The beacon path
   index starts over at the first path.
4. The eight front-panel lamps walk once. CMD then stays lit, unless `MYCALL`
   is still `N0CALL`, in which case CMD blinks at 2 Hz until the callsign is
   changed. The DCD lamp
   follows radio carrier. STA lights for 400 ms after each valid received frame.
   MULT lights while another frame is waiting in the transmit queue.
   CON lights while a frame is waiting in the viscous queue.
   The serial port then presents the callsign as the prompt, omitting SSID 0.
5. The foreground loop is one service pass: the 10 ms timers, the modem, the
   lamps, and one terminal character. The `/SYNCB` interrupt runs at 1200 Hz,
   and 12 interrupts queue one 10 ms tick. The loop does
   not pet the watchdog. `0xF8` is read only while the radio is deliberately
   keyed. When those reads stop, the watchdog releases PTT.

Cold-boot defaults:

| Parameter | Default | Accepted range |
|---|---|---|
| MYCALL | `N0CALL-0` | AX.25 call, SSID 0–15 |
| DIGIPEAT | on | off or on |
| DIRECTONLY | off | off or on |
| VISCOUS | off | off, or minimum and maximum seconds from 1–9 |
| LOGGING | on | off or on |
| TELEMETRY | on | off or on |
| TELPATH | `-` | one callsign, or `-` for no path |
| TXDELAY | 30 (300 ms) | 0–120, in 10 ms steps |
| PPERSIST | 63 | 0–255 |
| SLOTTIME | 10 (100 ms) | 0–255, in 10 ms steps |
| FULLDUPLEX | off | off or on |
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
immediately, and `TXDELAY` is used only when the radio keys up. Half duplex
waits until the channel is clear, then keys when a draw from 0 to 255 is
less than or equal to `PPERSIST`. Otherwise it waits one `SLOTTIME` and
draws again. Full duplex keys without that wait. A frame sent while the
radio stays keyed does not draw again.

At the callsign prompt, a config name alone prints the value stored in SRAM.
`NAME VALUE` updates that value when it is in range. `DISPLAY` prints every
setting. `HELP` prints where to read the documentation. `ENGSTAT` prints
the seconds counter, how many duplicate slots are occupied, the received and
transmitted frames since the previous telemetry report, and the `!R`, `!Q`, and `!S` counts since boot. `REBOOT`
starts the firmware over and keeps the stored settings. `RESET` clears the
stored settings and reboots, so the boot loads the defaults. Unknown commands
print `Huh?`; lines longer than 79 characters print
`Too long?`. [COMMANDS.md](COMMANDS.md) describes the line editor and the
meaning of each command.

An n-N prefix is at most five characters, so the hop-limit digit still fits in
the six-character AX.25 callsign. `WIDE` matches `WIDE2-2` and `WIDE2-1`: the
callsign is the prefix plus a digit N from 1 to 7, and the SSID is the
remaining hop count n, with n from 1 through N. It does not match a bare
`WIDE` or a hop count above N, such as `WIDE2-3`.

A repeat searches the whole path, not only the next unused address. `MYCALL`
is taken first and every hop through it is marked repeated. A path in which
`MYCALL` is already repeated has looped and is not sent again. An alias is
replaced by `MYCALL`. An n-N address is replaced by `MYCALL`, and the same
n-N call is appended at the end with the SSID reduced by one when that SSID
is still at least 1. `WIDE2-2` goes out as `MYCALL*,WIDE2-1`. `WIDE2-1` goes
out as `MYCALL*`. `DIRECTONLY` limits alias and n-N repeats to a path that
has not been used yet. A path that already has `MAXHOPS` repeated digipeaters
is not repeated. An unused address counts as one hop unless it matches a
configured n-N prefix, in which case it counts as the remaining hop count. A
used digipeater counts as one hop. The hop total is those used hops plus the
remaining request. A total equal to `MAXHOPS` is repeated normally. A larger
total is quashed: the matched n-N is decremented by one, every digipeater is
marked used, and `MYCALL` is appended so the last repeated hop is this station.
One used hop followed by `WIDE2-2` therefore goes out as `WIDE2-1*,MYCALL*`
when `MAXHOPS` is 2. When the path already
holds eight digipeaters, `MYCALL` replaces the last one. A path with no
matching address is left alone.

`VISCOUS X Y` enables `DIRECTONLY` and holds a selected direct repeat for a
random `X` through `Y` seconds. A copy heard through another digipeater during
that delay suppresses the queued repeat. Otherwise the frame moves to the
transmit queue when its delay expires. `VISCOUS OFF` restores immediate
digipeating and turns `DIRECTONLY` off. `DIRECTONLY OFF` turns `VISCOUS` off.

The SCC interrupt controller runs in Z80 mode 2. Radio HDLC receive interrupts
stay enabled, and the transmit-empty interrupt feeds each byte of a frame.
The terminal channel interrupts at 1200 Hz from the `/SYNCB` square wave,
and also on each received character. Those characters wait in a buffer.
RTS drops while the buffer is filling and rises after the service loop has
taken them, so a pasted configuration can wait instead of overrunning the
SCC. Twelve interrupts
queue one 10 ms tick, and 100 of those ticks queue one second. The last 250
packets this station transmits are kept for 30 seconds, each as the source
callsign and SSID, a one-byte sum of the printable characters in the
information field, and the second it was sent. While `VISCOUS` is enabled,
copies received through another digipeater are kept there too. A sum of 0 is an empty slot. A digipeat of the same source and
information field inside that window is skipped, and older entries are
cleared when the list is scanned. The beacon countdown runs in those seconds, shortened by
a random 0–31 seconds each time it is armed.
