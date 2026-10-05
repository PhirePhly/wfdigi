#ifndef WFDIGI_H
#define WFDIGI_H

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
#define RR0_TX_EOM 0x40u
#define RR0_ABORT 0x80u

/* RR1: end of frame, CRC error, overrun, and the SDLC residue field. */
#define RR1_END_FRAME 0x80u
#define RR1_CRC_ERR 0x40u
#define RR1_OVERRUN 0x20u
#define RR1_RESIDUE 0x0Eu
/* A byte-aligned CRC leaves residue code 011. */
#define RR1_RESIDUE_OK 0x06u

#define WR5_RTS 0x02u
#define WR5_TX_ENABLE 0x08u
#define WR5_BREAK 0x10u
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

/* Configuration image at the base of battery SRAM.
 * A CRC-16 is stored in the two bytes immediately after this image.
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
/* Eight digipeater addresses is the AX.25 path limit. */
#define MAXHOPS_MAX 8u
/* Software countdown slots, in 10 ms ticks. A tick is 12 interrupts of the 1200 Hz /SYNCB input.
 * One hundred ticks are one second. The duplicate window and the beacon interval use that second.
 */
#define DUPE_WINDOW 30u
/* Recently transmitted packets kept for the duplicate check. */
#define DUPE_SLOTS 250u
#define TIMER_TXDELAY 0u
#define TIMER_TXTAIL 1u
#define TIMER_TXWAIT 2u
#define TIMER_STA 3u
#define TIMER_SLOTTIME 4u
#define TIMER_COUNT 8u
/* STA stays lit for this many 10 ms ticks after a valid received frame. */
#define STA_TICKS 40u
/* CAL tones. High is the 2200 Hz space, low is the 1200 Hz mark. */
#define CAL_LOW 0u
#define CAL_HIGH 1u
#define CAL_BOTH 2u
#define CAL_SECONDS_MAX 30u

typedef struct {
    uint8_t mycall[CALLSIGN_LEN];
    uint8_t mycall_ssid;
    uint8_t digipeat;
    /* When set, alias and n-N repeats require an unused path. MYCALL does not. */
    uint8_t directonly;
    uint8_t txdelay;
    uint8_t persist;
    uint8_t slottime;
    uint8_t fullduplex;
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
     * beacon ("-"). The next slot is working RAM, not part of this image.
     */
    uint8_t bpath_count;
    uint8_t bpath[BPATH_SLOTS][CALLSIGN_LEN];
    uint8_t bpath_ssid[BPATH_SLOTS];
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
    uint8_t logging;
    uint8_t telemetry;
    /* A blank call is a direct telemetry frame ("-"). */
    uint8_t telpath[CALLSIGN_LEN];
    uint8_t telpath_ssid;
    uint8_t valid;
} DigiConfig;

_Static_assert(sizeof(DigiConfig) <= 255u, "config image must fit in one clear loop");
_Static_assert(CONFIG_ADDR + sizeof(DigiConfig) + 2u <= 0x8100u,
               "config image and CRC overlap compiler RAM at 0x8100");

extern DigiConfig __at (CONFIG_ADDR) g_config;

/* Revision from `git describe --tags --always --dirty` at build time.
 * The boot banner prints this. Telemetry should use this same string.
 */
extern const char wfdigi_version[];

void firmware_boot(void);
/* Enter the reset vector. Interrupts stop and the stack is discarded. */
void firmware_reset(void);

void hardware_set_im2(void);
void hardware_irq_off(void);
void hardware_irq_on(void);
/* Enables CPU interrupts and remembers that later critical sections may turn them back on. */
void hardware_irq_enable(void);
/* Turns CPU interrupts back on only after hardware_irq_enable. */
void hardware_irq_restore(void);
/* Strobes the PTT watchdog. Call only while this station is deliberately keying the radio. */
void hardware_watchdog_pet(void);
/* Assert or release radio PTT. While keyed, each call also pets the watchdog. */
void hardware_ptt(bool keyed);
/* Terminal RTS. Ready means the host may send. The caller holds interrupts off. */
void hardware_terminal_rts(bool ready);
/* Select the calibration waveform. Packet HDLC is restored by hardware_cal_restore. */
void hardware_cal_tone(uint8_t tone);
void hardware_cal_restore(void);
void hardware_init(void);
void hardware_lamps(uint8_t lamps_on);
bool hardware_radio_dcd(void);
/* Refresh the front-panel lamps from the current radio, queue, and interlock state. */
void lamps_service(void);

void serial_putc(uint8_t byte);
void serial_puts(const char *text);
/* Wait until the last terminal byte has left the transmitter. */
void serial_flush(void);
/* Arm the terminal receive interrupt and start with RTS ready. */
void serial_rx_init(void);
/* Store one byte from the channel B receive interrupt. May drop RTS. */
void serial_rx_push(uint8_t byte);
/* Stores the decimal form in dest and returns how many bytes were stored.
 * width 0 omits leading spaces. width 1-5 space-pads on the left. dest holds
 * five bytes. There is no trailing NUL. Zero still stores a digit.
 */
uint8_t format_u16(uint16_t value, uint8_t width, uint8_t *dest);
/* Decimal on the terminal, with no leading zeros. Zero prints as 0. */
void print_u16(uint16_t value);
bool serial_getc(uint8_t *byte);
/* Writes one byte when the terminal transmitter is idle. */
bool serial_try_putc(uint8_t byte);

/* Keep the battery image when its CRC matches. Otherwise load the defaults. */
void config_boot(void);
/* Recompute the CRC stored after the configuration image. */
void config_seal(void);
void config_command(char *line);
/* True while MYCALL is still N0CALL. The transmitter stays off and CMD blinks. */
extern bool tx_interlock;
/* Set tx_interlock from MYCALL. Called at the top of each service pass. */
void check_tx_interlock(void);

void cli_start(void);
void cli_input(uint8_t byte);
/* MYCALL> with a trailing space. A non-zero SSID is included, as in MYCALL-3>. */
const char *cli_prompt(void);
/* Reprint the callsign prompt and any partial line. Used after a modem trace. */
void cli_redraw(void);

void modem_quiesce(void);
void modem_init(void);
void modem_service(void);
/* Seconds since boot, occupied duplicate slots, and the !R and !Q counts. */
void engine_stat(void);
/* !R and !Q counts since boot. Another drop at 65535 reboots. */
void modem_drop_counts(uint16_t *frame_drops, uint16_t *queue_drops);
#define PKTQ_AX25 0u
#define PKTQ_BEACON 1u

void pktq_init(void);
/* True while at least one frame is still waiting in the transmit queue. */
bool pktq_pending(void);
/* Copy a frame into 31-byte blocks. False when the pool or the 64-deep queue is full. */
bool pktq_put(uint8_t kind, const uint8_t *data, uint16_t len);
/* Copy the oldest frame into dest and return its blocks to the pool. */
bool pktq_take(uint8_t *kind, uint8_t *dest, uint16_t dest_max, uint16_t *len);

/* Queue one AX.25 frame, without the CRC. The modem sends it when the radio is free. */
bool modem_send(uint8_t kind, const uint8_t *frame, uint16_t len);
/* Repeat a received frame when DIGIPEAT is on and the path matches this station. */
void digi_ingress(const uint8_t *frame, uint16_t len);
/* Clear the recently-sent list. Call before any packet is transmitted. */
void dupe_init(void);
/* Remember a frame that has gone out on the air. */
void dupe_remember(const uint8_t *frame, uint16_t len);
/* True when this source and information field were sent less than DUPE_WINDOW seconds ago. */
bool dupe_recent(const uint8_t *frame, uint16_t len);
/* Occupied duplicate slots. An aged entry stays until the next scan clears it. */
uint16_t dupe_count(void);
bool modem_keyed(void);
bool modem_dcd(void);
/* Key and send one calibration tone for the given number of seconds. */
void modem_calibrate(uint8_t tone, uint8_t seconds);
/* Unkey and restore HDLC. Called when the calibration second count reaches zero. */
void modem_cal_stop(void);

void timer_init(void);
void timer_service(void);
/* One edge of the 1200 Hz /SYNCB square wave. Called from the channel B external-status ISR. */
void timer_sync_edge(void);
void timer_set(uint8_t slot, uint16_t ticks_10ms);
bool timer_running(uint8_t slot);
bool timer_expired(uint8_t slot);
/* Free-running seconds since timer_init. Wraps after about 18 hours. */
uint16_t timer_seconds(void);
/* Lit half of a 2 Hz blink: 250 ms on, 250 ms off. */
bool timer_blink(void);
/* True when heard_at is less than DUPE_WINDOW seconds ago. */
bool timer_in_dupe_window(uint16_t heard_at);
/* Starts the beacon second countdown over. A zero interval stays idle. */
void timer_beacon_restart(void);
/* Queue one beacon now and arm the next interval. False if the queue did not accept it. */
bool timer_beacon_now(void);
/* Count down in tock_on_second, then modem_cal_stop runs. */
void timer_cal_start(uint8_t seconds);

void prng_init(void);
void prng_stir(uint16_t extra);
uint8_t prng_u8(void);

/* Seconds until the next beacon, already shortened by 0-31. Zero if beacons are off. */
uint16_t beacon_next_wait(void);
/* The beacon path cycle starts at the first path. */
void beacon_path_reset(void);
/* Build the position beacon and hand it to the modem. */
bool beacon_send(void);

/* Count one CRC-good received frame. The count runs with telemetry off. */
void telemetry_note_rx(void);
/* Count one frame that went out on the air. The count runs with telemetry off. */
void telemetry_note_tx(void);
/* Increase since the previous telemetry report. Both counters roll over. */
void telemetry_packet_counts(uint16_t *received, uint16_t *transmitted);
/* Zero the packet counts and arm the schedule from g_config.telemetry. */
void telemetry_init(void);
/* Start the telemetry schedule over. The packet counts keep running. */
void telemetry_restart(void);
/* One-second service for the 10-minute report and the hourly definition message. */
void telemetry_second(void);

uint8_t cli_pending_len(void);
char cli_pending_char(uint8_t index);

void isr_b_tx(void);
void isr_b_ext(void);
void isr_b_rx(void);
void isr_b_special(void);
void isr_a_tx(void);
void isr_a_ext(void);
void isr_a_rx(void);
void isr_a_special(void);

#endif
