# UIDIGI-compatible TAPR TNC-2

The `tnc2` target follows the UIDIGI/KISS-TNC2 hardware conventions. The ROM is 32 KiB at `0x0000`, RAM is 32 KiB at `0x8000`, and the mode-2 vector table is at `0x0100`. Channel A is the HDLC radio. Channel B is the asynchronous terminal. The image uses TNCID `APWFD2`.

## I/O ports

| Port | Function |
|---|---|
| `0xDC` | SIO channel A radio data |
| `0xDD` | SIO channel A radio control/status |
| `0xDE` | SIO channel B terminal data |
| `0xDF` | SIO channel B terminal control/status |

There is no LED latch and no software TX-watchdog port. The Z80 SIO/0 has no SCC WR9 master-interrupt gate.

## Clocks

The SIO uses externally supplied radio and terminal clocks, so the firmware does not program baud-rate generators or a DPLL. The terminal baud rate is set by the board's hardware jumpers.

Channel A is SDLC at x1 (`WR4` = `0x20`) on those external clocks. Channel B is asynchronous, x16, one stop bit, no parity (`WR4` = `0x44`).

Channel-B sync/hunt supplies a 600 Hz timer interrupt, so six interrupts make one 10 ms tick.

## Front panel and modem controls

The two software-controlled front-panel outputs are the SIO channel A and B DTR pins, normally labelled STA and CON. Both are active low. WFDIGI uses STA for a successfully decoded packet and CON for a packet waiting in the viscous digipeater queue. The other logical lamp states are not displayed.

PTT is channel A RTS. Terminal flow control is channel B RTS. There is no 7910 equalizer control: `EQUALIZER` does not change the hardware.

The TNC-2 has no PK-88-style software TX-watchdog strobe. PTT stays under channel A RTS alone.
