# PK-88 hardware

Z80 at 4.9152 MHz. Pin numbers are the Z8530 40-pin DIP from the Zilog SCC Technical Manual (September 1986, Figure 1-4). A leading slash is an active-low pin.

## Memory

| Range | Device |
|---|---|
| `0x0000-0x7fff` | 32 KiB EPROM |
| `0x8000-0xffff` | 32 KiB battery-backed SRAM |

## I/O ports

| Port | Access | Function |
|---|---|---|
| `0xF0` | read/write | Z8530 channel B control and status. Asynchronous RS-232 user-terminal port. |
| `0xF1` | read/write | Z8530 channel B data. |
| `0xF2` | read/write | Z8530 channel A control and status. HDLC interface to the 7910 Bell 202 AFSK modem. |
| `0xF3` | read/write | Z8530 channel A data. |
| `0xF4` | write | Octal D-latch, front-panel LEDs, active low. |
| `0xF8` | read | Watchdog service strobe. The value read is discarded. |

`A0` selects the SCC register: `0` is control, `1` is data, matching the Z8530 `D//C` pin (`D//C` high selects data). CPU `A1` drives the SCC `A//B` input directly. The SCC selects channel A when this input is high, so `0xF2`/`0xF3` is channel A and `0xF0`/`0xF1` is channel B.

### Channel identity

Physical channel A, port `0xF2`, is the radio channel:

- WR4 is `0x20`: SDLC/HDLC, x1 clock.
- WR10 is `0xA0`: NRZI, the AX.25 line code.
- WR11 is `0x66`: receive clock from the DPLL, transmit clock from the `/RTxC` pin.
- WR12/WR13 hold the baud-rate time constant. For 1200 baud the DPLL needs a 32× clock, which is time constant `0x003E` from the 4.9152 MHz PCLK.
- RR0 bit 3 (`/DCD`) drives the front-panel DCD LED.
- WR5 bit 1 (`/RTS`) is radio PTT and drives the SEND LED.

Physical channel B, port `0xF0`, is the RS-232 user terminal:

- WR4 is `0x44`: x16 clock, one stop bit, no parity.
- WR10 is `0x00`: NRZ.
- WR11 is `0x56`: both clocks come from the baud-rate generator.
- The terminal rate is fixed at 9600 baud. From the 4.9152 MHz PCLK that is time constant `0x000E` in WR12/WR13.

## Front-panel LEDs, port `0xF4`

One write byte, active low. A `0` in a bit lights that lamp. Writing `0x7F` lights only MULT.

| Bit | Lamp | Meaning |
|---|---|---|
| 0 | CONV | Converse mode |
| 1 | TRANS | Transparent mode |
| 2 | CMD | Command mode |
| 3 | SEND | Radio PTT is asserted (SCC channel A `/RTS`) |
| 4 | DCD | Carrier detect (SCC channel A `/DCD`) |
| 5 | STA | Unacknowledged packet frames |
| 6 | CON | A packet link is connected |
| 7 | MULT | More than one connection |

## Watchdog, port `0xF8`

A read pets the watchdog. The port is not written.

Radio PTT is the SCC channel A `/RTS` pin. Reading `0xF8` services the timer that releases PTT if the CPU stops petting it. When the reads stop, the watchdog expires and PTT drops.

## Z8530 pins

Leave `/W//REQ` disabled (WR1 bit 7 clear) and WR11 bit 7 clear. Neither channel uses its `SYNC`/`RTxC` crystal oscillator.

### Bus and package

| Pin | Name | PK-88 function |
|---|---|---|
| 1 | D1 | Z80 data bus |
| 2 | D3 | Z80 data bus |
| 3 | D5 | Z80 data bus |
| 4 | D7 | Z80 data bus |
| 5 | `/INT` | Z80 interrupt input. The CPU uses interrupt mode 2 |
| 6 | IEO | Daisy-chain output. The board has a single SCC |
| 7 | IEI | Daisy-chain input. Tied active for that single SCC |
| 8 | `/INTACK` | Interrupt acknowledge. Z80 mode 2 acknowledge is `/M1` with `/IORQ`, decoded onto this pin |
| 9 | VCC | +5 V |
| 20 | PCLK | SCC clock. Both baud-rate generators use PCLK (WR14 = `0x03`) |
| 31 | GND | Ground |
| 32 | `D//C` | Register select. High = data (`0xF1`, `0xF3`); low = control (`0xF0`, `0xF2`) |
| 33 | `/CE` | Chip enable for the `0xF0-0xF3` decode |
| 34 | `A//B` | Channel select. High = channel A = 7910 modem (`0xF2`/`0xF3`); low = channel B = RS-232 (`0xF0`/`0xF1`) |
| 35 | `/WR` | Z80 write |
| 36 | `/RD` | Z80 read |
| 37 | D6 | Z80 data bus |
| 38 | D4 | Z80 data bus |
| 39 | D2 | Z80 data bus |
| 40 | D0 | Z80 data bus |

### Channel A — 7910 Bell 202 modem (`0xF2` / `0xF3`)

WR11 = `0x66`: receive clock is the digital PLL, transmit clock is the `/RTxCA` pin, and `/TRxCA` outputs the baud-rate generator. WR4 selects SDLC at x1 and WR10 selects NRZI.

| Pin | Name | Direction | Function |
|---|---|---|---|
| 10 | `/W//REQA` | output | Wait/DMA request. Left disabled |
| 11 | `/SYNCA` | input | Sync/hunt input. WR15 = `0xD8` enables its external-status interrupt |
| 12 | `/RTxCA` | input | Transmit clock for the HDLC transmitter |
| 13 | RxDA | input | HDLC receive data from the 7910 modem. The SCC DPLL recovers the receive clock |
| 14 | `/TRxCA` | output | Baud-rate generator clock output |
| 15 | TxDA | output | HDLC transmit data to the 7910 modem |
| 16 | `/DTR//REQA` | output | 7910 `MC0`, driven by WR5 bit 7. The pin is high when that bit is clear |
| 17 | `/RTSA` | output | Radio PTT, active low. WR5 bit 1 is the key and lights SEND |
| 18 | `/CTSA` | input | Radio `CTS`, available in RR0 bit 5; its external-status interrupt is disabled |
| 19 | `/DCDA` | input | Radio carrier detect, active low. RR0 bit 3 drives the DCD LED, and WR15 enables its external-status interrupt |

### Channel B — RS-232 user terminal (`0xF0` / `0xF1`)

WR11 = `0x56`: receive and transmit clocks are the baud-rate generator, and `/TRxCB` outputs that generator. The generator is clocked from PCLK.

| Pin | Name | Direction | Function |
|---|---|---|---|
| 21 | `/DCDB` | input | Terminal `DCD`. The front-panel DCD lamp does not use this pin |
| 22 | `/CTSB` | input | Terminal `CTS`, active low, in RR0 bit 5 |
| 23 | `/RTSB` | output | Terminal `RTS`, WR5 bit 1 |
| 24 | `/DTR//REQB` | output | 7910 `MC1`, driven by WR5 bit 7. The pin is high when that bit is clear |
| 25 | TxDB | output | Terminal transmit data to the RS-232 line driver |
| 26 | `/TRxCB` | output | Baud-rate generator clock output |
| 27 | RxDB | input | Terminal receive data from the RS-232 line receiver |
| 28 | `/RTxCB` | input | TTL clock input. Not selected as the receive or transmit clock |
| 29 | `/SYNCB` | input | 74HC4020 `Q12`, clocked by the 4.9152 MHz CPU clock. `Q12` is ÷4096, so the pin is a 1200 Hz square wave. In asynchronous mode RR0 bit 4 follows the pin, and WR15 bit 4 enables an external-status interrupt at 1200 Hz |
| 30 | `/W//REQB` | output | Wait/DMA request. Left disabled |

`/DTRA` is 7910 `MC0` and `/DTRB` is 7910 `MC1`. Because `/DTR` is active low, a clear WR5 bit 7 holds the corresponding MC pin high. `MC2` and `MC3` are tied low, and `MC4` is tied high.

Those straps select the 7910 same-band modes. In this group the transmitter and receiver filters use the same frequency pair, which is the four-wire arrangement used by the radio. The AMD mode names call this loopback; the board does not jumper the modem back to itself. MC1 and MC0 select:

| MC1:MC0 | `MC4–MC0` | 7910 mode |
|---|---|---|
| `00` | `10000` | Bell 103 originate, 300 bps |
| `01` | `10001` | Bell 103 answer, 300 bps |
| `10` | `10010` | Bell 202, 1200 bps |
| `11` | `10011` | Bell 202 with compromise equalizer, 1200 bps |

Equalized Bell 202 (`MC4–MC0` = `10011`) is the 1200 bps packet mode.
