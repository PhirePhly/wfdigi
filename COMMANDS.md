# Commands

The terminal port is 9600 baud, 8 data bits, no parity, and one stop bit.
After the lamp test the TNC prints `WF> ` and waits for a line.

The current firmware stores these settings in battery SRAM. It does not
digipeat or send beacons yet. Every reset runs a cold boot, which copies
`include/config.h` back over the SRAM image and replaces any values entered
at the prompt.

## Entering a line

A line ends with carriage return or line feed. A line feed immediately after
a carriage return is ignored, so a CR/LF pair runs the command once. An empty
line prints the prompt again.

Printable characters are echoed. Backspace (`0x08`) and DEL (`0x7F`) erase
the last character. Other control characters are ignored. A line of more than
79 characters is discarded, and the TNC prints `?`.

The command name is not case sensitive. Callsigns and n-N prefixes are stored
in uppercase. Beacon text keeps the case you type. On `MYSYMBOL`, a lowercase
overlay letter is stored as uppercase, and the symbol code keeps its case.

A setting name alone prints the value stored in SRAM. `NAME VALUE` stores a
new value when it is in range, then prints that stored value. A rejected
value is left unchanged and reported as `Bad config: NAME`. An unknown
command prints `?`.

`DISPLAY` takes no value. It prints every setting as `NAME VALUE`, in the
same form that setting command accepts. `DISPLAY` followed by anything else
prints `?`.

```
WF> MYCALL
N0CALL-0
WF> MYCALL W6FOO-3
W6FOO-3
WF> DISPLAY
MYCALL W6FOO-3
DIGIPEAT ON
...
WF>
```

## DISPLAY

Prints every setting and its current value. A disabled alias or n-N prefix
prints as `OFF`. An empty beacon text prints as `-`. A beacon interval of 0
prints as `0`.

## MYCALL

This station's AX.25 address. Enter `CALL` or `CALL-SSID`. The callsign is
one to six characters, `A`–`Z` and `0`–`9`, with no embedded spaces. The SSID
is 0–15; omitting it stores 0, and the value is always printed with the SSID,
including `-0`. `MYCALL` cannot be turned off. The cold-boot default is
`N0CALL-0`.

## DIGIPEAT

Turns digipeating on or off. Accepts `ON`, `OFF`, `1`, or `0`. When it is
on, this station is willing to repeat packets addressed to `MYCALL`, an
enabled alias, or a matching n-N prefix. The cold-boot default is `ON`.

## TXDELAY

How long the radio is keyed before the first byte of a frame, in 10 ms steps.
The range is 0–120, so 30 means 300 ms. The cold-boot default is 30. This is
separate from the end of a transmission: every transmission sends 3 HDLC
flags and then unkeys. That closing rule is fixed and is not a command.

## PERSIST

The CSMA persistence threshold, 0–255. When the channel is free, the TNC
draws a number from 0 to 255 and keys the transmitter if the draw is less
than or equal to `PERSIST`. A larger draw waits one `SLOTTIME` and tries
again. The cold-boot default is 63.

## SLOTTIME

The channel-access slot used with `PERSIST`, in 10 ms steps. The range is
0–255, so 10 means 100 ms. The cold-boot default is 10.

## FULLDUP

Selects full duplex. Accepts `ON`, `OFF`, `1`, or `0`. When it is on, the
TNC transmits without waiting for a clear channel. When it is off, transmit
timing follows carrier detect, `PERSIST`, and `SLOTTIME`. The cold-boot
default is `OFF`.

## ALIAS0, ALIAS1, ALIAS2, ALIAS3

Extra callsigns this station digipeats, in addition to `MYCALL`. Each slot
is independent, and there is no alias count. A slot holds one AX.25 callsign
and SSID in the same form as `MYCALL`. `OFF` or `-` disables that slot. A
disabled slot prints as `OFF`. All four slots are off at cold boot.

## NNALIAS0, NNALIAS1, NNALIAS2, NNALIAS3

APRS n-N digipeat prefixes. Each slot stores a prefix only, with no SSID.
The prefix is at most five characters, `A`–`Z` and `0`–`9`, so the hop-limit
digit still fits in the six-character AX.25 callsign. `OFF` or `-` disables
that slot. A disabled slot prints as `OFF`. All four slots are off at cold
boot.

On the air, the callsign is the prefix plus one digit N from 1 to 7, and the
SSID is the remaining hop count n, with n from 1 through N. `WIDE` matches
`WIDE2-2` and `WIDE2-1`. It does not match a bare `WIDE`, and it does not
match a hop count above N, such as `WIDE2-3`.

## BEACON

How often this station beacons, in whole minutes. The range is 0–60. A value
of 0 turns the beacon off. `OFF` or `-` stores 0. The printed value is the
number of minutes, including `0`. The cold-boot default is 0.

## BTEXT

The text carried in a beacon. The value is the rest of the line after
`BTEXT`, including spaces, and the case is preserved. It must be printable
ASCII and at most 63 characters. A value of `-` alone clears it. An empty
beacon prints as `-`. The cold-boot default is empty.

## BPATH

The digipeater paths used by the beacon. Enter one to four paths, separated
by spaces. Each path is one AX.25 callsign, in the same form as `MYCALL`, or
`-` for a beacon with no path. `-` is a path the beacon still uses; it does
not remove a slot the way it does for an alias. More than four paths is
rejected. The cold-boot default is `-`.

Each beacon takes the next configured path and then starts over at the first.
`BPATH WIDE1-1 - -` beacons via `WIDE1-1`, then with no path, then with no
path again, so one beacon out of three requests a hop. Setting `BPATH` starts
that cycle over at the first path.

## MYLOC

This station's position, in degrees and decimal minutes:

```
lat-degrees lat-minutes N|S lon-degrees lon-minutes E|W
```

Latitude degrees are 0–90 and longitude degrees are 0–180. Minutes are
0.00–59.99. The minutes may be a whole number, or have one or two digits
after the decimal point: `0.5` means 0.50. Leading zeros are optional on
input. The stored position is always printed with latitude degrees in two
digits, longitude degrees in three digits, and minutes as `MM.hh`, for
example `02 44.30 N 024 03.06 W`. A position of exactly 90° or 180° is
accepted only with 0 minutes. The cold-boot default prints as
`00 00.00 N 000 00.00 E`.

## MAXHOPS

The most digipeater hops a packet may have for this station to repeat it.
The range is 1–7, which is the APRS n-N limit. The cold-boot default is 3.

## MYSYMBOL

The two-character APRS symbol used for this station. The first character
selects the table: `/` for the primary table, `\` for the alternate table,
or an overlay `0`–`9` or `A`–`Z`. The second character is the symbol code,
from `!` through `~`. The cold-boot default is `/#`, the primary-table
digipeater symbol.
