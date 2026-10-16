# Front Panel Display Lights

When WFDIGI first powers on, it blinks each light in sequence to make it
possible to easily validate that the TNC is no longer running the stock
AEA PK-88 firmware.

`CONV`: During power-on, the `CONV` light is lit while the TNC is performing the
CRC validation check of the entire boot ROM, which can take several seconds.

`TRANS`: The `TRANS` light is lit when the Z80 is in the halt condition.
It is normal for this light to be dimly lit, and go completely dark when the
CPU is busy processing a packet or a command.

`CMD`: Solid indicates that a valid config is active and the TNC is ready to
transmit. If the `CMD` light is blinking, this indicates that the TNC is current
experiencing a transmitter interlock because the callsign is still set to
`N0CALL`

`SEND`: Indicates that the radio is keyed and we are sending a packet

`DCD`: Data Carrier Detect indicates that we are receiving audio from the
radio and attempting to decode AX.25 frames. The PK-88 expects squelched audio
from the radio, so if this light is lit continuously it indicates that the
squelch settings on the radio need to be adjusted or the incorrect interface
pin on the radio was used.

`STA`: Flashes when a packet is successfully decoded and received with a
valid CRC over the radio

`CON`: Indicates that one or more packets are in the viscous digipeating
packet queue waiting to see if the same packet is digipeated by any other
digipeaters in the area before being sent ourselves.

`MULT`: Indicates that additional packets are waiting in the transmit packet
queue to be sent out. A single packet will normally move straight from the
TX packet queue to the modem send buffer, so this light indicates at least a
second packet waiting to go on the air.
