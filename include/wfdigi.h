#ifndef WFDIGI_H
#define WFDIGI_H

#include "ax25.h"
#include "platform.h"

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef uint8_t bool;
#define true 1
#define false 0

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

/* Logical lamp states. A platform maps the states it can display to hardware. */
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
#define ALIAS_COUNT 4u
#define NNALIAS_COUNT 4u
/* One character of the AX.25 callsign is the hop-limit digit, so the prefix
 * stored here is at most 5 characters.
 */
#define NNALIAS_LEN 5u
#define BTEXT_LEN 64u
#define BPATH_SLOTS 4u
/* Stations whose source or used digipeater hop causes a received frame to be dropped. */
#define DROP_COUNT 10u

#define TXDELAY_MIN 0u
#define TXDELAY_MAX 120u
#define PERSIST_MIN 0u
#define PERSIST_MAX 255u
#define SLOTTIME_MIN 0u
#define SLOTTIME_MAX 255u
/* HDLC flags sent after the last frame. PTT drops once these are done. */
#define TX_CLOSING_FLAGS 3u
#define BEACON_MAX 60u
#define MAXHOPS_MIN 1u
/* Eight digipeater addresses is the AX.25 path limit. Zero in the setting disables it. */
#define MAXHOPS_MAX AX25_MAX_VIAS
/* Recently transmitted packets kept for the duplicate check. */
#define DUPE_SLOTS 250u
/* Software countdown slots, in 10 ms ticks. EDGES_PER_TICK is platform-specific.
 * One hundred ticks are one second. The duplicate window and beacon interval use that second.
 */
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
    /* When set, alias and n-N repeats require an unused first hop. MYCALL does not. */
    uint8_t directonly;
    /* Calls that discard a received frame when they are the source or a used hop.
     * A blank call disables that slot. SSID_ANY matches every SSID of that call.
     */
    uint8_t droplist[DROP_COUNT][CALLSIGN_LEN];
    uint8_t droplist_ssid[DROP_COUNT];
    /* Both zero disables viscous delay. Otherwise 1 <= min <= max <= 9 seconds. */
    uint8_t viscous_min;
    uint8_t viscous_max;
    uint8_t txdelay;
    uint8_t persist;
    uint8_t slottime;
    /* Seconds a frame may sit in the TX queue before it goes stale (!S). */
    uint8_t txstale;
    uint8_t fullduplex;
    /* A blank call disables that slot. */
    uint8_t alias[ALIAS_COUNT][CALLSIGN_LEN];
    uint8_t alias_ssid[ALIAS_COUNT];
    /* n-N prefixes, with no SSID. A blank prefix disables that slot.
     * On air the callsign is the prefix plus one digit N (1-7), and the SSID
     * is the remaining hop count n, 1 <= n <= N. WIDE matches WIDE2-2 and
     * WIDE2-1, and does not match a bare WIDE. An SSID above N, such as
     * WIDE1-2, is quashed in place as WIDE1-2*,MYCALL* without taking a hop.
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
    /* 1 through MAXHOPS_MAX, or 0 to repeat any hop count. */
    uint8_t maxhops;
    /* APRS symbol table ('/', '\\', or overlay 0-9/A-Z) and code ('!'..'~'). */
    uint8_t symbol_table;
    uint8_t symbol_code;
    uint8_t logging;
    uint8_t telemetry;
    /* A blank call is a direct telemetry frame ("-"). */
    uint8_t telpath[CALLSIGN_LEN];
    uint8_t telpath_ssid;
    /* When set, the 7910 uses Bell 202 with the DSP equalizer. */
    uint8_t equalizer;
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
/* PLATFORM_NAME from the selected platform.h, such as "pk88" or "tnc2". */
extern const char wfdigi_platform[];

void firmware_boot(void);
/* Enter the reset vector. Interrupts stop and the stack is discarded. */
void firmware_reset(void);

void hardware_set_im2(void);
void hardware_irq_off(void);
void hardware_irq_on(void);
void hardware_halt(void);
/* Enables CPU interrupts and remembers that later critical sections may turn them back on. */
void hardware_irq_enable(void);
/* Turns CPU interrupts back on only after hardware_irq_enable. */
void hardware_irq_restore(void);
/* Strobes the PTT watchdog. Call only while this station is deliberately keying the radio. */
void hardware_watchdog_pet(void);
/* Assert or release radio PTT. While keyed, each call also pets the watchdog. */
void hardware_ptt(bool keyed);
/* Bell 202 with the compromise equalizer when on, and Bell 202 when off. */
void hardware_equalizer(bool on);
/* Terminal RTS. Ready means the host may send. The caller holds interrupts off. */
void hardware_terminal_rts(bool ready);
/* Select the calibration waveform. Packet HDLC is restored by hardware_cal_restore. */
void hardware_cal_tone(uint8_t tone);
void hardware_cal_restore(void);
void hardware_init(void);
void hardware_lamps(uint8_t lamps_on);
bool hardware_radio_dcd(void);
uint8_t hardware_radio_rr0(void);
uint8_t hardware_radio_rr1(void);
uint8_t hardware_radio_data_read(void);
void hardware_radio_data_write(uint8_t value);
void hardware_radio_command(uint8_t command);
void hardware_radio_wr1(uint8_t value);
uint8_t hardware_terminal_rr0(void);
uint8_t hardware_terminal_data_read(void);
void hardware_terminal_data_write(uint8_t value);
void hardware_terminal_command(uint8_t command);
void hardware_terminal_wr1(uint8_t value);
/* Handle and acknowledge a terminal external/status interrupt. True is one timer edge. */
bool hardware_timer_interrupt(void);
void hardware_timer_arm(void);
/* Enable any controller-global interrupt gate after modem setup. */
void hardware_interrupt_master_enable(void);
/* Refresh the front-panel lamps from the current radio, queue, and interlock state. */
void lamps_service(bool system_idle);

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
/* A lowercase letter becomes uppercase. Any other byte is returned unchanged. */
uint8_t letter_to_upper(uint8_t c);
/* True for an ASCII letter or decimal digit. */
bool is_alphanumeric(uint8_t c);
/* ASCII APRS callsign in TNC2 form. An AX.25 station fits in six characters
 * plus a numeric SSID. An APRS-IS station may use the whole nine characters,
 * and its SSID may be one or two alphanumeric characters.
 */
typedef struct {
    uint8_t text[TNC2_CALLSIGN_LEN];
} tnc2_callsign;
/* Write an AX.25 callsign and SSID as TNC2 text. SSID 0 is omitted. pad fills
 * the unused tail: a space in an APRS message, or 0 when the text is printed.
 * A nine-character result fills dest and leaves no trailing NUL.
 */
void format_tnc2_callsign(tnc2_callsign *dest, const uint8_t *call, uint8_t ssid, uint8_t pad);
/* 8-bit Pearson hash of len bytes at data. An empty range hashes to 0. */
uint8_t pearson_hash(const uint8_t *data, uint16_t len);
/* CRC-16/CCITT-FALSE: polynomial 0x1021, initial value 0xFFFF, no final XOR. */
uint16_t crc16_feed(uint16_t crc, uint8_t byte);
/* True when the last boot found a matching ROM checksum. */
extern bool rom_checksum_pass;
/* True when the 7-byte AX.25 address matches this callsign and SSID.
 * call holds CALLSIGN_LEN plain characters. The H bit is ignored.
 */
bool call_eq(const uint8_t *ax, const uint8_t *call, uint8_t ssid);
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
/* True for this boot when the defaults were loaded. Warm boot clears it. */
extern bool cold_boot;

void cli_start(void);
void cli_input(uint8_t byte);
/* MYCALL> with a trailing space. A non-zero SSID is included, as in MYCALL-3>. */
const char *cli_prompt(void);
/* Reprint the callsign prompt and any partial line. Used after a trace line. */
void cli_redraw(void);

/* Clear the trace ring. Battery SRAM is not wiped, so call this at boot. */
void logging_init(void);
/* Send one queued trace byte when the terminal transmitter is idle. */
void logging_service(void);
/* Append one monitored frame when LOGGING is on. kind is R, T, D, or V. */
void logging_frame(char kind, const uint8_t *frame, uint16_t len);

void modem_quiesce(void);
void modem_init(void);
void modem_service(void);
/* Seconds since boot, occupied duplicate slots, and the !R, !Q, !S, and !F counts. */
void engine_stat(void);
/* !R, !Q, !S, and !F counts since boot. Another drop at 65535 reboots. */
void modem_drop_counts(uint16_t *frame_drops, uint16_t *queue_drops, uint16_t *stale_drops,
                       uint16_t *format_drops);
/* Print !F and count a format drop. A frame that cannot be rewritten, or a
 * received frame with no source callsign. Another drop at 65535 reboots.
 */
void note_drop_f(void);
#define PKTQ_TX 0u
#define PKTQ_VISCOUS 1u
#define PKTQ_COUNT 2u
/* Longest delay pktq_expire_in will schedule. A longer request is clamped. */
#define PKTQ_EXPIRE_SPAN 0x8000u

void pktq_init(void);
/* Absolute second at which a slot is due. seconds is clamped to PKTQ_EXPIRE_SPAN. */
uint16_t pktq_expire_in(uint16_t seconds);
/* True once timer_seconds() has reached expire, including that same second. */
bool pktq_expired(uint16_t expire);
/* True while this queue holds a frame. */
bool pktq_pending(uint8_t queue);
/* Read the first slot's expiry without removing it. */
bool pktq_peek_expire(uint8_t queue, uint16_t *expire);
/* Copy a frame into 31-byte blocks on one queue, in expiry order. expire is
 * the due second. A frame that is due with or after the last slot is appended.
 * False when the queue id is unknown, or the pool or that 16-deep ring is full.
 */
bool pktq_put(uint8_t queue, const uint8_t *data, uint16_t len, uint16_t expire);
/* Copy the frame that expires first and its expiry second. expire may be null.
 * False when that queue has nothing left.
 */
bool pktq_take(uint8_t queue, uint8_t *dest, uint16_t dest_max, uint16_t *len, uint16_t *expire);

/* Seconds until a queued TX frame goes stale (!S). Default 10.
 * Zero disables the timeout.
 */
#define TX_STALE_DEFAULT 10u

/* Queue one AX.25 frame on the transmit queue, without the CRC. expires_in is
 * how many seconds until the slot is due. The modem sends it when the radio
 * is free, or counts !S if that second has already been reached.
 */
bool modem_send(const uint8_t *frame, uint16_t len, uint16_t expires_in);
/* Queue a viscous frame without kicking the transmitter. */
bool modem_queue_viscous(const uint8_t *frame, uint16_t len, uint16_t expire);
/* The held receive buffer has been copied. The receiver may reuse it. */
void modem_rx_release(void);
/* Copy a received frame, release the modem buffer, then repeat it when it
 * matches. False when the frame was not copied; the caller still owns the buffer.
 */
bool digi_ingress(const uint8_t *frame, uint16_t len);
/* Promote due viscous frames or suppress them when another copy was heard. */
void digi_service(void);
/* Messaging has no cooldown of its own. Call before any packet is received. */
void messaging_init(void);
/* True when this frame is a directed message for MYCALL and must not be digipeated.
 * A general ?APRS? may shorten the beacon countdown and still returns false.
 */
bool process_frame_for_messaging(const uint8_t *frame, uint16_t len);
/* Queue one UI frame from MYCALL to the platform TNCID. The text is the information field.
 * The first BPATH entry is the digipeater when that entry is a callsign.
 * Returns 0 when queued, 1 when the text does not fit, and 2 when the
 * transmit queue is full.
 */
uint8_t messaging_send_raw(const char *info);
/* Clear the recently-sent list. Call before any packet is transmitted. */
void dupe_init(void);
/* Remember a frame that has gone out on the air. */
void dupe_remember(const uint8_t *frame, uint16_t len);
/* True when this source and information field were sent less than DUPE_WINDOW seconds ago. */
bool dupe_recent(const uint8_t *frame, uint16_t len);
/* Occupied duplicate slots. An aged entry stays until the next scan clears it. */
uint16_t dupe_count(void);
/* Stations heard directly. When the list is full, the oldest entry makes room. */
#define HEARD_SLOTS 50u
/* An entry is answered for 8 hours. timer_seconds wraps after about 18 hours,
 * so the sweep must clear an entry well before its age can wrap.
 */
#define HEARD_MAX_AGE 28800u
#define HEARD_SWEEP_EVERY 3600u
/* Stations named in one ?APRSD reply. A longer list takes another message. */
#define HEARD_PAGE 5u
/* Clear the heard list. Battery SRAM is not wiped, so call this at boot. */
void heard_init(void);
/* Note the 7-byte AX.25 source address of a frame no digipeater has repeated yet. */
void heard_note(const uint8_t *ax_source);
/* Clear entries HEARD_MAX_AGE seconds old or older. */
void heard_sweep(void);
/* The next page of calls heard within HEARD_MAX_AGE, in table order and
 * separated by spaces. *from is 0 to start at the first slot, and is left
 * pointing at the last call written when another page remains, or 0 when the
 * list is finished. That point is only good until the next heard_note or
 * heard_sweep. At most HEARD_PAGE whole calls that fit in dest_max are
 * written. Returns the length, 0 when nothing remains.
 */
uint16_t heard_list(uint8_t *dest, uint16_t dest_max, const void **from);
bool modem_keyed(void);
bool modem_dcd(void);
/* Key and send one calibration tone for the given number of seconds. */
void modem_calibrate(uint8_t tone, uint8_t seconds);
/* Unkey and restore HDLC. Called when the calibration second count reaches zero. */
void modem_cal_stop(void);

void timer_init(void);
void timer_service(void);
/* One platform timer interrupt/edge, called from the channel B external-status ISR. */
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
/* Seconds left on the beacon countdown. Zero when it is due or beacons are off. */
uint16_t timer_beacon_left(void);
/* Replace that countdown. The next second sends when it is zero and beacons are on. */
void timer_beacon_set(uint16_t seconds);
/* A general ?APRS? draws one delay in 0..GENERAL_QUERY_WINDOW seconds. */
#define GENERAL_QUERY_WINDOW 120u
/* True after a general query has already adjusted this beacon countdown. */
bool timer_beacon_accelerated(void);
/* Mark this countdown. Cleared when the beacon countdown reaches zero. */
void timer_beacon_accelerate(void);
/* Count down in tock_on_second, then modem_cal_stop runs. */
void timer_cal_start(uint8_t seconds);
/* Hold query position beacons for DUPE_WINDOW seconds. The timer counts it down. */
void timer_start_query_cooldown(void);
/* True while that hold still has seconds left. */
bool timer_query_cooling(void);

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
/* Count one viscous repeat suppressed by a copy heard from another station. */
void telemetry_note_viscous(void);
/* Increase since the previous telemetry report. Both counters roll over. */
void telemetry_packet_counts(uint16_t *received, uint16_t *transmitted);
/* Zero the packet counts and arm the schedule from g_config.telemetry. */
void telemetry_init(void);
/* Start the telemetry schedule over. The packet counts keep running. */
void telemetry_restart(void);
/* One-second service for the 10-minute report and the four-hour definition message. */
void telemetry_second(void);
/* Send BITS, EQNS, PARM, and UNIT now. A copy still inside the dupe window is skipped. */
void telemetry_send_definitions(void);

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
