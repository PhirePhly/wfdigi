# AEA PK-232

Z80 at 4 MHz. The Z8530 PCLK is 2.4576 MHz. Channel A is the HDLC radio modem. Channel B is the asynchronous serial terminal. The EPROM is 32 KiB at `0x0000`. SRAM is 16 KiB at `0xC000`.

## Memory

| Range | Device |
|---|---|
| `0x0000-0x7fff` | 32 KiB EPROM |
| `0x8000-0xbfff` | optional 16kB expansion ROM |
| `0xc000-0xffff` | 16 KiB SRAM |

## I/O ports

| Port | Access | Function |
|---|---|---|
| `0x7C` | read/write | Z8530 channel A control. HDLC radio modem |
| `0x7D` | read/write | Z8530 channel A data |
| `0x7E` | read/write | Z8530 channel B control. Asynchronous serial terminal |
| `0x7F` | read/write | Z8530 channel B data |
| `0xBC` | write | HDLC command port. SCC WR0 commands |
| `0xBD` | read/write | Data written with the `0xBC` commands. Bit 3 follows radio RTS |
| `0xBE` | read/write | Z8536 CIO parallel data. Front-panel lamps |
| `0xBF` | read/write | Z8536 CIO control. Register number, then value |

`A0` is the SCC `D//C` pin: even ports are control, odd ports are data. `A1` selects the channel, so `0x7C`/`0x7D` are channel A and `0x7E`/`0x7F` are channel B. `A6` is decoded. `0xBC`/`0xBD` are a separate pair from `0x7C`/`0x7D`, and `0xBE`/`0xBF` are the CIO rather than the SCC.

Both baud-rate generators use the 2.4576 MHz PCLK. Channel A divides that clock to 1200 baud for the HDLC modem. The DPLL needs a 32× clock, so the WR12/WR13 time constant is `0x001E`. Channel B divides it to 9600 baud for the terminal with an ×16 clock, so the time constant is `0x0006`.

## Front-panel LEDs

The lamps are Z8536 GPIO pins. Four of them are wired straight to a pin:

| Pin | Lamp |
|---|---|
| PC0 | STA |
| PC1 | CON |
| PC2 | MULT |
| PB3 | SEND |

PB4–PB7 go to a 1-of-10 decoder that selects the packet mode. PB0–PB2 go to a decoder for the remaining lamps. This firmware drives those pins low and does not use the decoders. A 1 on PC0, PC1, PC2, or PB3 turns that lamp on.

Port C and port B data are Z8536 registers `0x0F` and `0x0E`, written through the control port `0xBF`. PC0–PC2 and every port B pin are outputs.

## Tick

Nothing feeds the SCC `/SYNCB` pin. Counter/timer 3 runs in continuous timer mode from the 4 MHz CPU clock. Timer mode counts every other PCLK, so a 10 ms period is time constant 20000 (`0x4E20`). The counter's vector is `0x02`, the SCC channel B external-status slot. Channel B is the terminal and that slot stays unused, so the tick takes it. Each interrupt is one tick.

The arming write to counter/timer 3's command register (`0x0C`) is `0xC6`: interrupt enable, software gate open, and trigger. The handler writes `0x24` back to the same register, which clears IP and IUS and leaves the gate open.

## WFDIGI

The image uses TNCID `APWFD3`. The 256-byte configuration page is at `0xC000`, the base of the battery SRAM, and compiler RAM follows at `0xC100`.

Channel A (`0x7C`/`0x7D`) is the 1200 baud HDLC radio. Channel B (`0x7E`/`0x7F`) is the 9600 baud terminal. The radio handlers occupy channel A's interrupt vectors, and the tick enters through channel B's external-status vector. `0xBC`/`0xBD` are not used.

Radio PTT is channel A `/RTS`, WR5 bit 1. The PTT watchdog is channel A `/DTR`, WR5 bit 7. Each pet toggles that bit so `/DTRA` changes state. If the toggles stop, the watchdog expires and PTT drops.

## Still open

- What, on the stock board, responds at `0x8000-0xBFFF`. The 1987 image tests `0x8000` for a `JP` opcode and sometimes calls `0x8000-0x8020`.
- Whether `0xBC`/`0xBD` are a latch beside the SCC or a second decode of one SCC channel. Bit 3 of `0xBD` follows radio RTS in that image. WFDIGI drives SEND through PB3 instead.
- Why the 1988 image is linked at `0x8000` and omits the data ports `0x7D` and `0x7F`.
