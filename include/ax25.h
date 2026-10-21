#ifndef AX25_INCLUDED
#define AX25_INCLUDED

/* Six callsign characters, then the SSID byte. The SSID is 0 through 15. */
#define CALLSIGN_LEN 6u
#define SSID_MAX 15u
/* Config-only: match every SSID of a callsign. Not an on-air SSID value. */
#define SSID_ANY 16u
#define AX25_ADDR 7u

/* Destination, source, and up to eight digipeaters. */
#define AX25_MAX_VIAS 8u
#define AX25_MAX_ADDRS (2u + AX25_MAX_VIAS)

/* Bit 7 of the SSID byte. On a digipeater this is has-been-repeated. */
#define AX25_H 0x80u
/* Bit 0 set marks the last address in the path. */
#define AX25_EXT 0x01u
/* The two reserved bits in the SSID byte stay set. */
#define AX25_RESERVED 0x60u

/* UI frame control and no-layer-3 PID. */
#define AX25_UI 0x03u
#define AX25_PID 0xF0u

/* Two addresses and a control byte. Shorter than this is not a frame. */
#define AX25_MIN ((AX25_ADDR * 2u) + 1u)
/* The modem accepts frames up to this length. */
#define AX25_MAX 330u

/* A transmitted frame is remembered this many seconds. */
#define DUPE_WINDOW 30u

#endif
