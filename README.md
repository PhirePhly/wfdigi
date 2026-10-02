# Whiskey Fox Digi

Standalone APRS digipeater firmware for the AEA PK-88 packet TNC. It is a
clean rewrite in C for the SDCC Z80 compiler, in the same spirit as UIDIGI on
the TAPR TNC-2: the board comes up as a digipeater, without the original
terminal command set.

The image in the tree today is the hardware bring-up. It initializes the SCC,
loads the default digipeater configuration into SRAM, and talks to a terminal
at 9600 baud. It does not digipeat yet.

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

## Firmware organization

- `src/startup.s`, `src/isr_stubs.s`: reset, the mode-2 vector table at
  `0x0100`, and the watchdog strobe
- `src/hardware.c`: SCC setup, front-panel lamps, radio carrier detect, and PTT
- `src/serial.c`: polled terminal I/O
- `src/cli.c`: `WF>` line editor
- `include/config.h`: cold-boot defaults. Edit this file and run `make`
- `src/config.c`: range checks and the SRAM image at `0x8000`
- `src/main.c`: boot banner, lamp test, and the foreground loop
- `src/interrupts.c`: mode-2 handlers. The SCC master interrupt enable is
  still off, so these are not called yet

## What the current ROM does

1. Program `build/wfdigi.bin` into a 32 KiB EPROM and cold-reset the PK-88.
2. A terminal set to 9600 8N1 receives:

    ```
    Whiskey Fox Digi - version 0.1
    Copyright 2026 - Kenneth Finnegan
    Cold boot...
    ```

3. Cold boot copies the defaults from `include/config.h` into the battery SRAM image at `0x8000`.
   Each value is range-checked as it is stored. A value outside its limits is
   left clear and reported as `Bad config: NAME`. The image is marked valid
   only when every parameter passes.
4. The eight front-panel lamps walk once, then CMD stays lit. The DCD lamp
   follows radio carrier. The serial port then presents a `WF>` prompt.
5. The foreground loop does not pet the watchdog. `0xF8` is read only while
   the radio is deliberately keyed. When those reads stop, the watchdog
   releases PTT.

Cold-boot defaults:

| Parameter | Default | Accepted range |
|---|---|---|
| MYCALL | `N0CALL-0` | AX.25 call, SSID 0–15 |
| DIGIPEAT | on | off or on |
| TXDELAY | 30 (300 ms) | 0–120, in 10 ms steps |
| PERSIST | 63 | 0–255 |
| SLOTTIME | 10 (100 ms) | 0–255, in 10 ms steps |
| FULLDUP | off | off or on |
| ALIAS 0–3 | blank, disabled | an AX.25 call and SSID, or empty to disable that slot |
| NNALIAS 0–3 | blank, disabled | an n-N prefix, or empty to disable that slot |
| BEACON | off | off, 1–60 minutes |
| BTEXT | empty | printable ASCII, NUL terminated |
| BPATH | `-` | 1–4 paths; a callsign, or `-` for no path |
| MYLOC | `00 00.00 N 000 00.00 E` | 0-90 0.0-59.99 [NS] 0-180 0.0-59.99 [EW] |
| MAXHOPS | 3 | 1-7 |
| MYSYMBOL | `/#` | two characters: primary `/`, alternate `\`, or overlay `0-9`/`A-Z`, then a symbol code |

Every transmission ends with 3 HDLC flags, then the radio is unkeyed.

At the `WF>` prompt, a config name alone prints the value stored in SRAM.
`NAME VALUE` updates that value when it is in range. `DISPLAY` prints every
setting. [COMMANDS.md](COMMANDS.md) describes the line editor and the meaning
of each command.

An n-N prefix is at most five characters, so the hop-limit digit still fits in
the six-character AX.25 callsign. `WIDE` matches `WIDE2-2` and `WIDE2-1`: the
callsign is the prefix plus a digit N from 1 to 7, and the SSID is the
remaining hop count n, with n from 1 through N. It does not match a bare
`WIDE` or a hop count above N, such as `WIDE2-3`.

The SCC interrupt controller is programmed for Z80 mode 2, but interrupts
remain masked until the digipeater has handlers that can retire them.
