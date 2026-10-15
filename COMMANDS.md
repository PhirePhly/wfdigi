# Commands

The terminal port is 9600 baud, 8 data bits, no parity, and one stop bit.
The host's CTS must follow this TNC's RTS. Each received character is taken
by an interrupt into a buffer. RTS turns off when that buffer is filling,
and turns back on after the service loop has read those characters. A pasted
`DISPLAY` listing needs that handshake. Without it, characters that arrive
while a command is still printing are dropped once the buffer is full.

After the lamp test the TNC prints a prompt of the callsign and waits for a
line. A non-zero SSID is included, so SSID 0 is `N0CALL> ` and SSID 3 is
`N0CALL-3> `. Changing `MYCALL` changes the next prompt.

The current firmware stores these settings in battery SRAM. A received frame
is repeated when `DIGIPEAT` is on and the path matches this station. A
non-zero `BEACON` interval queues a position beacon. A 16-bit CRC is stored
in the two bytes after the configuration image. When that CRC matches, reset
keeps the settings and prints `Warm boot...`. When it does not, reset copies
`include/config.h` over the image and prints `Cold boot...`.

## Entering a line

A line ends with carriage return or line feed. A line feed immediately after
a carriage return is ignored, so a CR/LF pair runs the command once. An empty
line prints the prompt again.

Printable characters are echoed. Backspace (`0x08`) and DEL (`0x7F`) erase
the last character. Other control characters are ignored. A line of more than
79 characters is discarded, and the TNC prints `Too long?`.

The command name is not case sensitive. Callsigns and n-N prefixes are stored
in uppercase. Beacon text keeps the case you type. On `MYSYMBOL`, a lowercase
overlay letter is stored as uppercase, and the symbol code keeps its case.

A setting name alone prints the value stored in SRAM. `NAME VALUE` stores a
new value when it is in range, then prints that stored value. A rejected
value is left unchanged and reported as `Bad config: NAME`. An unknown
command prints `Huh?`.

The symbol `-` is used to represent an empty or null string. This can be used
to disable some settings and express a empty value for other settings.

`DISPLAY`, `HELP`, `ENGSTAT`, `REBOOT`, and `RESET` take no value. `DISPLAY`
prints every setting as `NAME VALUE`, in the same form that setting command
accepts. `HELP` prints where to read the documentation. `ENGSTAT` prints the
packet-engine snapshot below. `REBOOT` starts the firmware over and keeps the
stored settings. `RESET` clears the stored settings and then reboots, so the
boot loads the defaults. Any of these followed by anything else prints `Huh?`.

```
N0CALL> MYCALL
N0CALL-0
N0CALL> MYCALL W6FOO-3
W6FOO-3
W6FOO-3> DISPLAY
MYCALL W6FOO-3
DIGIPEAT ON
...
W6FOO-3>
```

## HELP

Prints where to read the documentation:

```
Visit https://github.com/PhirePhly/wfdigi for documentation
```

## DISPLAY

Prints every setting and its current value. A disabled alias or n-N prefix
prints as `OFF`. An empty beacon text prints as `-`. A beacon interval of 0
prints as `OFF`.

## ENGSTAT

Prints a snapshot of the packet engine:

```
TIME 123
DUPEDB 2
RX 40
TX 12
!R 0
!Q 1
!S 0
!F 0
```

`TIME` is the 16-bit seconds counter from boot, the same counter a trace line
prints. `DUPEDB` is how many of the 250 duplicate slots are occupied. An entry
that has aged out still counts until the next duplicate scan clears it. `RX`
and `TX` are the frames counted since the previous telemetry report, the same
numbers the next telemetry frame will send. Reading `ENGSTAT` does not move
that baseline. Both counters are 16-bit and roll over; the printed value is
the unsigned difference, so a rollover still counts the frames in the
interval. They keep counting while `TELEMETRY` is off. `!R`, `!Q`, `!S`, and `!F` are how
many times those drop codes have been printed since boot. Each is a 16-bit
count. Another drop after a counter reaches 65535 reboots the TNC. The
counts are working RAM, not part of the battery configuration image, and
every boot starts them at zero.

## REBOOT

Starts the firmware from the reset vector, the same path as a hardware reset.
Battery SRAM is kept. A matching configuration CRC prints `Warm boot...` and
the settings stay. The timers, modem, packet queue, duplicate list, drop
counters, and received and transmitted frame counts start at zero. The beacon
path index starts over at the first path.

## RESET

Zeros the configuration page in battery SRAM, then does what `REBOOT` does.
The checksum no longer matches, so the boot prints `Cold boot...` and copies
the defaults from `include/config.h`.

## MYCALL

This station's AX.25 address. Enter `CALL` or `CALL-SSID`. The callsign is
one to six characters, `A`–`Z` and `0`–`9`, with no embedded spaces. The SSID
is 0–15; omitting it stores 0, and the value is always printed with the SSID,
including `-0`. `MYCALL` cannot be turned off. The cold-boot default is
`N0CALL-0`. The transmitter will not key while the callsign is `N0CALL`.
A frame that reaches the radio is discarded, and the TNC prints
`ERR - Set Callsign`. While that interlock is active, the CMD lamp blinks at
2 Hz. After `MYCALL` is changed, CMD stays lit.

## DIGIPEAT

Turns digipeating on or off. Accepts `ON`, `OFF`, `1`, or `0`. When it is
on, a received frame is repeated when its path contains `MYCALL`, an enabled
alias, or a matching n-N prefix. The cold-boot default is `ON`. A frame this
station originated is not repeated.

The path is searched with preemption, in this order. `MYCALL` is tried first,
anywhere in the path, including when earlier hops are already used. Every
digipeater up through `MYCALL` is marked repeated. `DIRECTONLY` does not
change that match. If `MYCALL` is already marked repeated, the packet has
looped and is left alone, even when `MYCALL` appears again later. A path that
already has `MAXHOPS` repeated digipeaters is not repeated.

If `MYCALL` is not in the path, the first unused alias is replaced with
`MYCALL`, marked repeated, and every digipeater before it is marked repeated
as well. If no alias matches, the first unused n-N address is handled the
same way, then its remaining hop count is reduced by one and written at the
end of the path when that count is still at least one. `WIDE2-2` becomes
`MYCALL*,WIDE2-1`. `WIDE2-1` becomes `MYCALL*`. Hops that followed the n-N
address stay where they were.

Each digipeat, message reply, and query-triggered telemetry definition is kept
in a list of 250. An entry holds the source callsign and SSID, a one-byte
Pearson hash of every byte in the information field, and the second the packet
was sent. A hash of 0 means the slot is empty, so a calculated hash of 0 is
stored as 1. The transmitted packet takes the first empty slot. Before a
digipeat or reply is queued, the list is scanned by that hash, then the source
callsign and the SSID. An entry 30 seconds old or older is cleared. A match that is still inside
that window is not sent. When every slot is still inside the window, the oldest
entry is replaced. A received general query is not stored, so it can still be
digipeated.

## DIRECTONLY

Limits alias and n-N digipeating to a path with no used hops yet.
Accepts `ON`, `OFF`, `1`, or `0`. The cold-boot default is `OFF`. Turning it
off also turns `VISCOUS` off.

When it is on, a packet is repeated for an alias or a matching n-N prefix
only when the first digipeater address does not have the has-been-repeated
bit set. `WIDE1-1,WIDE2-1` qualifies as direct, where `W6WWW*,WIDE2-1` is
dropped. A reduced n-N address such as `WIDE2-1` does not by itself
make the path used, which is different from some other direct-only APRS
implementations. 

A packet addressed to `MYCALL` is repeated even when the first hop has
already been used.

## VISCOUS

`VISCOUS X Y` delays a digipeat by a random whole number of seconds from
`X` through `Y`, inclusive. Both values must be 1–9 and `X` cannot be greater
than `Y`. Applying the command turns `DIRECTONLY` on. `VISCOUS OFF`
disables the delay and turns `DIRECTONLY` off. `DIRECTONLY OFF` also turns
this delay off. The cold-boot default is `OFF`. A recommended first value to
try if you are interested in using this feature is `VISCOUS 3 5`

When a direct packet is selected for digipeating, its rewritten frame enters
the viscous queue instead of the transmit queue. It waits a random number of
seconds between `X` and `Y` and if another station has repeated the
same source and information field, the queued frame is discarded. Otherwise
it moves to the transmit queue and is sent out. A packet
whose path specifically contains `MYCALL` bypasses the viscous queue and is
sent immediately, including bypassing the `DIRECTONLY` filter.

## LOGGING

Prints every frame the radio modem receives or sends. Accepts `ON`, `OFF`,
`1`, or `0`. The cold-boot default is `ON`.

A received frame is printed as:

```
R   123 N0CALL>APRS,WIDE1-1:Hello
```

A frame whose source callsign has no printable character is printed the same
way with `D` in place of `R`, and it is not digipeated. Six spaces and an
SSID, which would have been shown as `-7`, is one such source. A sent frame
uses the same layout with `T` in place of `R`. A viscous frame suppressed
because another station repeated it uses `V`; this shows what this station
would otherwise have transmitted. The timestamp is
the rolling 16-bit seconds counter, printed right-aligned in five characters
so packet text stays aligned. Each trace starts and ends with a new line, and
the callsign prompt is redrawn under it, including any characters already
typed. Lines share a 256-byte ring. The main loop sends one byte each pass, so
the 10 ms timers keep running and a later frame can be logged before the
earlier line has finished. When 240 bytes are still waiting, the ring writes
`!!!` and discards further trace text until the backlog has drained to 128
bytes. An SSID of 0 is omitted. The last digipeater that
has already repeated the frame is marked with `*`, as in `WIDE1-1*`.
Bytes in the payload that are not printable ASCII are left out of the trace,
so it stays on one line. A CRC-good frame that is not AX.25 is printed as
hexadecimal instead. `LOGGING OFF` keeps the modem running and suppresses
these lines.

## TELEMETRY

Sends APRS telemetry. Accepts `ON`, `OFF`, `1`, or `0`. The cold-boot default
is `ON`. Received and transmitted
frame counts keep running either way; `ENGSTAT` prints the increase since
the previous report. The counts are not stored in the battery image. Every
boot starts them at zero.

Boot and a change to this setting start the PARMS/UNITS schedule over.
The first definition message waits five minutes to protect the local network from
an antenna failure causing the TNC to reset and creating an AIO style boot loop
immediately keying the radio again.

One definition message follows every four hours, in this order: `BITS`, `EQNS`, `PARM`, `UNIT`.

```
:MYCALL   :BITS.11111100,WFDIGI <version>
:MYCALL   :EQNS.0,0.1,0,0,0.1,0,0,0.1,0,0,1,0,0,1,0
:MYCALL   :PARM.RxPkts,TxPks,ViscousDrops,AdverseDrops,,COLD,SUM,!F,!Q,!R,!S
:MYCALL   :UNIT.pkts/min,pkts/min,pkts/min,count,
```

`EQNS` divides the received, transmitted, and viscous channels by 10, so a
10-minute packet count is shown on a graph as packets per minute.

Every 10 minutes a telemetry frame is sent out. The digipeater path used is
controlled by `TELPATH`, and the definition messages use that same path:

```
T#000,<received>,<transmitted>,<viscous>,<drops>,0,bbbbbbbb
```

`<received>`, `<transmitted>`, and `<viscous>` are the frames counted since
the previous report. `<viscous>` counts a delayed repeat discarded because
another station had already repeated the same source and information field.
The counters are 16-bit and roll over. The value is their unsigned
difference, so a rollover still counts the frames in that interval.
`<drops>` is how far the `!R`, `!Q`, `!S`, and `!F`
counters have moved since the previous report. The eight digits are digital
bits B1 through B8. B1 stays `1` for the whole boot when that boot printed
`Cold boot...`, and stays `0` after `Warm boot...`. B2 stays `1` for the whole
boot when that boot printed `ROM checksum: PASS`, and stays `0` after
`ROM checksum: FAIL`. B3 through B6 are `1` when `!F`, `!Q`, `!R`, and `!S`
each moved since the previous report. B7 and B8 are unused and are always `0`.
`BITS.11111100` marks B1 through B6 as the bits in use. The sequence
number runs from 000 through 999. Nothing is queued while `MYCALL` is still
`N0CALL`. A directed `?APRSU` query, described under Messages, sends all four
definition messages at once and does not move this schedule.

## TELPATH

The digipeater path used by telemetry. Enter one AX.25 callsign, in the same
form as `MYCALL`, or `-` for a direct frame with no path. The cold-boot default
is `-`, so the 10-minute report and the definition messages do not
request a digipeater.

## TXDELAY

How long the radio sends HDLC flags before the first byte when it keys up
and acquires the channel. The range is 0–120 steps of 10 ms, so 30 means
300 ms of preamble to allow the radio to key up and remote squelch to open.
The cold-boot default is 30. Additional frames in the transmit queue do not
trigger this same preamble when the first packet finishes: the radio stays keyed,
sends three closing flags, and starts that frame immediately since the channel
is already aquired and synced.

## PPERSIST

The CSMA persistence threshold, 0–255. When the channel is free, the TNC
draws a number from 0 to 255 and keys the transmitter if the draw is less
than or equal to `PPERSIST`. A larger draw waits one `SLOTTIME` and tries
again. If carrier returns during that wait, the TNC waits for the channel
to clear and then draws again. A frame already waiting while the radio stays
keyed does not draw again. The cold-boot default is 63.

## SLOTTIME

The channel-access slot used with `PPERSIST`, in 10 ms steps. The range is
0–255, so 10 means 100 ms. A value of 0 draws again on the next service
pass. The cold-boot default is 10.

## FULLDUPLEX

Selects full duplex. Accepts `ON`, `OFF`, `1`, or `0`. When it is on, the
TNC transmits without waiting for a clear channel. When it is off, transmit
timing follows carrier detect, `PPERSIST`, and `SLOTTIME`. The cold-boot
default is `OFF`.

## TXSTALE

Seconds a queued frame may wait before it is counted as stale (`!S`) and
dropped. Accepts 0–255. A value of 0 or `OFF` disables the stale timeout
entirely, so a queued frame never goes stale. When it is 10 (the cold-boot
default), `DISPLAY` does not print it. Set it to any other value and it
appears in `DISPLAY`.

## EQUALIZER

Enables the AM7910 receive equalizer. Accepts `ON`, `OFF`, `1`, or `0`.
When it is off, the modem uses Bell 202. When it is on, the modem uses Bell
202 with the DSP equalizer. The cold-boot default is `ON`.

Note that there is essentially no consensus across the APRS community whether
AX.25 should be emphasized or flat, so it is not at all clear what the correct
setting is for this knob.
Kenneth's recommendation is to assume that APRS is transmitted flat, so leave this
setting on if your receive pathway has de-emphasis in it.

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
`WIDE2-2` and `WIDE2-1`. It does not match a bare `WIDE`. An SSID above n,
such as `WIDE1-2`, is invalid and quashed in place without taking a hop:
the address stays `WIDE1-2*`, every digipeater is marked repeated, and 
`MYCALL*` is appended. That quash does not depend on `MAXHOPS`.

## BEACON

How often this station beacons, in whole minutes. The range is 0–60. A value
of 0 turns the beacon off. `OFF` or `-` stores 0. A disabled beacon prints as
`OFF`. Any other value prints as the number of minutes. The cold-boot default
is off.

The interval is converted to seconds and counted down once a second. Each
time it is armed, including when you set a new value, a random 0–31 seconds
is subtracted so the beacons are not spaced by the exact same interval. When
the count reaches zero the TNC assembles one UI position frame and puts it
on the transmit queue, then arms the next interval. The queue holds many
frames in 31-byte blocks, so a beacon does not wait for the radio to go
idle. If the queue is full, it tries again on the next second.

The frame is `MYCALL` to `APWFDP`, then the current `BPATH` entry, with an
APRS position report: `!` latitude, symbol table, longitude, symbol code,
then `BTEXT`. Latitude is `DDMM.hh` and longitude is `DDDMM.hh`. An empty
`BPATH` entry is sent with no digipeater, and the path advances only after
the frame is queued.

## BSEND

Queues one beacon immediately, the same way the beacon timer does when its
countdown reaches zero. `BSEND` takes no value; extra text prints `Huh?`. It
sends even when `BEACON` is 0. After the frame is queued, the countdown
starts over from the current interval, including the random 0–31 second
offset. A zero interval stays idle after that one beacon. If the transmit
queue cannot take the frame, the TNC prints `Busy` and leaves the countdown
at zero so the next second tries again while `BEACON` is non-zero.

## SENDRAW

Queues one UI frame and returns to the prompt. It is not a stored setting and
does not appear in `DISPLAY`. A bare `SENDRAW` prints `Huh?`.

```
SENDRAW packet contents
```

The frame is `MYCALL` to `APWFDP`. The packet contents are the information
field, including spaces, and the case is preserved. The digipeater is the
first `BPATH` entry when that entry is a callsign. A blank first entry, or no
beacon path, is sent direct. `SENDRAW` does not advance the
beacon-path cycle. With `BPATH WIDE1-1`, `SENDRAW >Test` goes out as
`MYCALL>APWFDP,WIDE1-1:>Test`.

Missing packet contents prints `Huh?`. Text that does not fit in the frame
prints `Bad config: SENDRAW`. If the transmit queue cannot take the frame, the
TNC prints `Busy`. `MYCALL` still has to be set, or the TNC prints
`ERR - Set Callsign` and does not key. The beacon countdown is left alone.

## CAL

Keys the radio and sends a Bell 202 calibration tone, then unkeys. It takes
two arguments: `H`, `L`, or `D`, and a duration from 1 to 30 seconds.
`CAL H 15` sends the high tone, 2200 Hz, for 15 seconds. `L` sends the low
tone, 1200 Hz. `D` sends HDLC flags, which use both symbols. A missing or
malformed argument prints `Huh?`. A duration of 0 or above 30 prints
`Bad config: CAL`. The transmitter must be idle; otherwise the TNC prints
`Busy`. `MYCALL` still has to be set, or the TNC prints `ERR - Set Callsign`
and does not key. `CAL` is not a stored setting and does not appear in
`DISPLAY`.

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
The range is 1–8. The cold-boot default is 3. One n-N address is still at most
7. Eight is the most digipeater addresses an AX.25 path can hold.

Each digipeater that has already been repeated counts as one used hop,
including a used n-N address such as `WIDE5-5*`. When that count is already
`MAXHOPS` or more, the packet is not repeated.

An unused address that matches a configured n-N prefix counts as its remaining
hop count. Any other unused address counts as one, including a call that looks
like n-N for a prefix this station does not have. The hop total is the used
hops plus that remaining request. A path with no `MYCALL`, alias, or n-N match
is left alone. A total equal to `MAXHOPS` is repeated normally: a fresh
`WIDE2-2` when this setting is 2 becomes `MYCALL*,WIDE2-1`, and `WIDE2-1` in
`AAA,BBB*,WIDE2-1` becomes `AAA,BBB,MYCALL*`. A larger total is quashed.
`AAA*,WIDE2-2` when this setting is 2 is one used hop plus two still requested,
so the matched n-N is decremented, every digipeater is marked repeated, and
`MYCALL` is appended at the end: `AAA*,WIDE2-1*,MYCALL*`. The last repeated
hop is this station. An alias is marked repeated in place the same way, and
`MYCALL` is still appended after it. `WIDE5-5` when this setting is 3 goes
out as `WIDE5-4*,MYCALL*`. If the path already has eight digipeaters, `MYCALL`
replaces the last one. A ninth address is not added.

## MYSYMBOL

The two-character APRS symbol used for this station. The first character
selects the table: `/` for the primary table, `\` for the alternate table,
or an overlay `0`–`9` or `A`–`Z`. The second character is the symbol code,
from `!` through `~`. The cold-boot default is `/#`, the primary-table
digipeater symbol.

Traditionally fill-in digipeaters use the `1#` symbol and Regional network digipeaters
use `S#`.

# Messages

An APRS message for this station is a UI frame whose information field starts
with `:CALLSIGN :`. The callsign field is nine characters. SSID 0 is omitted,
and any other SSID is written after a dash. The comparison ignores letter
case. That message is answered and is not digipeated. This is the addressee
in the information field. An AX.25 path that contains `MYCALL` is still
handled by `DIGIPEAT`.

A message number is the last `{` followed by one to five letters or digits.
A numbered message is answered at once with `ack` and that number, before any
other reply. The number is copied as received. A message without a valid
number is not acknowledged.

An acknowledgement, and the unsupported-query reply below, is a UI frame from
`MYCALL` to `APWFDP`. The first `BPATH` entry is its one digipeater when that
entry is a callsign. A blank first entry, or no beacon path, is sent direct.
The reply does not advance the beacon-path cycle. It is recorded in the
duplicate list before it is queued, so the same reply is not sent again for
30 seconds. The transmitter interlock still discards it while `MYCALL` is
`N0CALL`.

A directed query is a message whose text is `?APRS` and one letter `A`–`Z`.
The character after that letter is the end of the text or a space. More text
may follow the space.

`?APRSP` queues the same position beacon as `BEACON` and starts that countdown
over. Another position query, from any station, does not queue a second beacon
until 30 seconds have passed. A numbered query is still acknowledged during
that wait, and an unsupported query is still answered. A periodic beacon does
not start or reset the wait. If the transmit queue cannot take the beacon, the
wait is not started and the beacon countdown stays at zero so the next second
tries again while `BEACON` is non-zero.

`?APRSU` queues all four telemetry definition messages at once, in the order
`BITS`, `EQNS`, `PARM`, `UNIT`. A copy of one of those messages that is still
inside the duplicate window is not queued again. The four-hour definition
schedule is left where it was. The query sends the definitions even when
`TELEMETRY` is off.

Any other letter is answered with the unnumbered message
`ERR - query type unsupported`.

`?APRS`, `?APRS `, and a directed `?APRS?` are ordinary messages. They are
acknowledged when numbered, and they do not request a beacon or a definition.

A general query is an information field whose first word is `?APRS?`. It may
queue a position beacon under the same 30-second wait, and the received frame
then continues through digipeating. Digipeating still has to be on and the
path still has to match.

A third-party packet has an information field that starts with `}`. The
originator is the callsign before `>` in that TNC-2 header, and the text after
the header's colon follows the same message and query rules. The AX.25 path
and the vias inside the header are not used for the reply. A header that
cannot be read is left for the digipeater. A third-party packet that is
neither a general query nor a message for `MYCALL` is also left for the
digipeater.
