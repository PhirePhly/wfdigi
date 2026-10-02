#ifndef PK88_H
#define PK88_H

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef uint8_t bool;
#define true 1
#define false 0

/* Z80 clock and the SCC PCLK are both 4.9152 MHz.
 * Time constant = PCLK / (2 * baud * clocks_per_bit) - 2.
 * Terminal: 4915200 / (2 * 9600 * 16) - 2 = 14.
 * Radio DPLL: 4915200 / (2 * 1200 * 32) - 2 = 62.
 * These are literals because SDCC evaluates preprocessor arithmetic in 16 bits
 * and would truncate 4915200. tools/check-layout.py checks the same formulas.
 */
#define TERMINAL_BRG_TC 14u
#define RADIO_DPLL_TC 62u

#define PORT_SCC_B_CTRL 0xF0u
#define PORT_SCC_B_DATA 0xF1u
#define PORT_SCC_A_CTRL 0xF2u
#define PORT_SCC_A_DATA 0xF3u
#define PORT_LED 0xF4u
#define PORT_WATCHDOG 0xF8u

#define RR0_RX_CHAR 0x01u
#define RR0_TX_EMPTY 0x04u
#define RR0_DCD 0x08u

#define WR5_RTS 0x02u
#define WR5_TX_ENABLE 0x08u
#define WR5_DTR 0x80u

/* Front-panel latch is active low. These masks are the lamps that should be on. */
#define LED_CONV 0x01u
#define LED_TRANS 0x02u
#define LED_CMD 0x04u
#define LED_SEND 0x08u
#define LED_DCD 0x10u
#define LED_STA 0x20u
#define LED_CON 0x40u
#define LED_MULT 0x80u

/* Cold-boot configuration image at the base of battery SRAM.
 * Timing values are in the usual 10 ms TNC units.
 */
#define CONFIG_ADDR 0x8000u
#define CALLSIGN_LEN 6u
#define ALIAS_COUNT 4u
#define NNALIAS_COUNT 4u
/* One character of the AX.25 callsign is the hop-limit digit, so the prefix
 * stored here is at most 5 characters.
 */
#define NNALIAS_LEN 5u
#define BTEXT_LEN 64u
#define BPATH_SLOTS 4u

#define TXDELAY_MIN 0u
#define TXDELAY_MAX 120u
#define PERSIST_MIN 0u
#define PERSIST_MAX 255u
#define SLOTTIME_MIN 0u
#define SLOTTIME_MAX 255u
#define SSID_MAX 15u
/* HDLC flags sent after the last frame. PTT drops once these are done. */
#define TX_CLOSING_FLAGS 3u
#define BEACON_MAX 60u
#define MAXHOPS_MIN 1u
#define MAXHOPS_MAX 7u

typedef struct {
    uint8_t mycall[CALLSIGN_LEN];
    uint8_t mycall_ssid;
    uint8_t digipeat;
    uint8_t txdelay;
    uint8_t persist;
    uint8_t slottime;
    uint8_t fulldup;
    /* A blank call disables that slot. */
    uint8_t alias[ALIAS_COUNT][CALLSIGN_LEN];
    uint8_t alias_ssid[ALIAS_COUNT];
    /* n-N prefixes, with no SSID. A blank prefix disables that slot.
     * On air the callsign is the prefix plus one digit N (1-7), and the SSID
     * is the remaining hop count n, 1 <= n <= N. WIDE matches WIDE2-2 and
     * WIDE2-1, and does not match WIDE or WIDE2-3.
     */
    uint8_t nnalias[NNALIAS_COUNT][CALLSIGN_LEN];
    uint8_t beacon_every;
    uint8_t btext[BTEXT_LEN];
    /* One to four beacon paths. A blank call in a used slot is a direct
     * beacon ("-"). Each beacon uses bpath_next, then advances it.
     */
    uint8_t bpath_count;
    uint8_t bpath[BPATH_SLOTS][CALLSIGN_LEN];
    uint8_t bpath_ssid[BPATH_SLOTS];
    uint8_t bpath_next;
    /* Degrees and hundredths of a minute. Hemispheres are 'N'/'S' and 'E'/'W'.
     * 90 and 180 degrees are stored only with 0.00 minutes.
     */
    uint8_t loc_lat_deg;
    uint8_t loc_lat_min;
    uint8_t loc_lat_hund;
    uint8_t loc_lat_ns;
    uint8_t loc_lon_deg;
    uint8_t loc_lon_min;
    uint8_t loc_lon_hund;
    uint8_t loc_lon_ew;
    uint8_t maxhops;
    /* APRS symbol table ('/', '\\', or overlay 0-9/A-Z) and code ('!'..'~'). */
    uint8_t symbol_table;
    uint8_t symbol_code;
    uint8_t valid;
} DigiConfig;

_Static_assert(sizeof(DigiConfig) <= 255u, "config image must fit in one clear loop");
_Static_assert(CONFIG_ADDR + sizeof(DigiConfig) <= 0x8100u,
               "config image overlaps compiler RAM at 0x8100");

extern DigiConfig __at (CONFIG_ADDR) g_config;

void firmware_boot(void);

void hardware_set_im2(void);
/* Strobes the PTT watchdog. Call only while this station is deliberately keying the radio. */
void hardware_watchdog_pet(void);
/* Assert or release radio PTT. While keyed, each call also pets the watchdog. */
void hardware_ptt(bool keyed);
void hardware_init(void);
void hardware_lamps(uint8_t lamps_on);
bool hardware_radio_dcd(void);

void serial_putc(uint8_t byte);
void serial_puts(const char *text);
bool serial_getc(uint8_t *byte);

void config_cold_boot(void);
void config_command(char *line);

void cli_start(void);
void cli_input(uint8_t byte);

void isr_b_tx(void);
void isr_b_ext(void);
void isr_b_rx(void);
void isr_b_special(void);
void isr_a_tx(void);
void isr_a_ext(void);
void isr_a_rx(void);
void isr_a_special(void);

#endif
