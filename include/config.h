#ifndef CONFIG_H
#define CONFIG_H

/* Cold-boot defaults copied into battery SRAM.
 * Edit this file, then run make.
 *
 * Calls are up to 6 characters, A-Z and 0-9, with no SSID in the text.
 * An empty alias or n-N prefix disables that slot. There is no slot count.
 * Timing values are in 10 ms steps. Beacon is in minutes, and 0 turns it off.
 * MYLOC is degrees and decimal minutes, for example "00 00.00 N 000 00.00 E".
 * MAXHOPS is 1 through 7.
 * MYSYMBOL is two characters: a table or overlay, then the symbol code.
 * BPATH is one to four beacon paths. "-" is a beacon with no path.
 */

#define CFG_MYCALL "N0CALL"
#define CFG_MYCALL_SSID 0

#define CFG_DIGIPEAT 1

#define CFG_TXDELAY 30
#define CFG_PERSIST 63
#define CFG_SLOTTIME 10
#define CFG_FULLDUP 0

#define CFG_ALIAS_0 ""
#define CFG_ALIAS_0_SSID 0
#define CFG_ALIAS_1 ""
#define CFG_ALIAS_1_SSID 0
#define CFG_ALIAS_2 ""
#define CFG_ALIAS_2_SSID 0
#define CFG_ALIAS_3 ""
#define CFG_ALIAS_3_SSID 0

#define CFG_NNALIAS_0 ""
#define CFG_NNALIAS_1 ""
#define CFG_NNALIAS_2 ""
#define CFG_NNALIAS_3 ""

#define CFG_BEACON 0
#define CFG_BTEXT ""
#define CFG_BPATH "-"

#define CFG_MYLOC "00 00.00 N 000 00.00 E"
#define CFG_MAXHOPS 3
#define CFG_MYSYMBOL "/#"

#endif
