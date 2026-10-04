# Debug codes

The terminal prints these when the firmware drops a frame. Each code is one
event, with no carriage return after it. Each print also adds one to a
16-bit counter that stops at 65535. `ENGSTAT` prints those counters. They
are cleared on every boot, including a warm boot.

## !R

A valid received AX.25 frame was discarded before it could be printed. The
receiver holds one completed frame while the next one is arriving. If that
next frame also finishes before the first is released, or a trace line is
still draining, the new frame is dropped and the TNC prints `!R`.

## !Q

A frame was not copied into the transmit queue. The queue holds 64 frames in
31-byte blocks. Either the block pool or the queue was full, so the frame is
not transmitted and the TNC prints `!Q`.
