# Debug codes

The terminal prints these when the firmware drops a frame. Each code is one
event, with no carriage return after it. Each print also adds one to a
16-bit counter. `ENGSTAT` prints those counters. They are cleared on every
boot, including a warm boot. Another drop after a counter has reached 65535
reboots the TNC.

## !R

A valid received AX.25 frame was discarded. The receiver holds one completed
frame while the next one is arriving. If that next frame also finishes before
the first is released, the new frame is dropped and the TNC prints `!R`.

## !Q

A frame was not copied into a packet queue. The transmit and viscous queues
each hold 16 frames and share one pool of 31-byte blocks. The pool or the
selected ring was full, so the frame is dropped and the TNC prints `!Q`.

## !S

A frame was taken from the transmit queue after its expiry second. Each
transmit frame is due 10 seconds after it was queued. The frame is not
transmitted and the TNC prints `!S`.

## !F

A frame was dropped because it is not a usable AX.25 format. A repeat that
needs one more digipeater address, and is already within 7 bytes of the
330-byte limit, is discarded and the TNC prints `!F`. A received frame with
no source callsign is logged as `D` when logging is on, then counted the
same way.
