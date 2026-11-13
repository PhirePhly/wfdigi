#include "hardware.h"

/* APRS telemetry. Received and transmitted totals live in working RAM, count
 * whether or not telemetry is on, and start over on every boot. They are
 * 16-bit and roll over. A UI frame every 10 minutes carries the unsigned
 * difference since the previous report. TELPATH is its digipeater; a blank
 * path is sent direct. The first definition waits five minutes after boot or
 * after telemetry is changed, so a transmission that resets the TNC does not
 * immediately key the radio again. One definition then goes out every four hours:
 * BITS, EQNS, PARM, then UNIT. A ?APRSU query sends all four at once.
 * The report's eight digital bits, left to right, are B1 cold boot for this
 * power-on, B2 ROM checksum passed, B3 !F, B4 !Q, B5 !R, and B6 !S since the
 * previous report. B7 and B8 are unused. BITS.11111100 marks that same set.
 * The BITS project name is WFDIGI followed by the same "<version> <platform>"
 * text that ?APRSV returns.
 * Analog channel five is channel occupancy in Erlangs. byte_count is the
 * bytes sent or received since its high byte was last reported, plus four
 * per frame for the opening flag, the closing flag, and the CRC. It saturates
 * at 65535. The report sends the high byte, and the low byte stays so a
 * short frame is still counted in a later report. EQNS multiplies that byte
 * by 0.002844 (1/351.56): 90000 bytes in the ten minutes would read as 1.0.
 * Preamble and bit stuffing are not counted. telemetry_restart zeros it.
 */
#define FRAME_MAX 128u
#define MSG_MAX (FRAME_MAX - 35u)
#define SLOT_SECONDS 600u
#define TELEMETRY_START_DELAY 300u
#define TELEMETRY_DEF_INTERVAL 14400u

static const char msg_eqns[] = "EQNS.0,0.1,0,0,0.1,0,0,0.1,0,0,1,0,0,0.002844,0";
static const char msg_parm[] = "PARM.RxPkts,TxPks,ViscousDrops,AdverseDrops,Erlangs,COLD,SUM,!F,!Q,!R,!S";
static const char msg_unit[] = "UNIT.pkts/min,pkts/min,pkts/min,count,E";

_Static_assert(sizeof(msg_eqns) - 1u <= MSG_MAX, "EQNS message is too long");
_Static_assert(sizeof(msg_parm) - 1u <= MSG_MAX, "PARM message is too long");
_Static_assert(sizeof(msg_unit) - 1u <= MSG_MAX, "UNIT message is too long");

static uint8_t frame[FRAME_MAX];
static uint16_t frame_n;
static bool frame_ok;

static volatile uint16_t rx_count;
static uint16_t tx_count;
/* Bytes on the channel since the high byte was last reported. Saturates. */
static volatile uint16_t byte_count;
static uint16_t last_rx;
static uint16_t last_tx;
static uint16_t last_r;
static uint16_t last_q;
static uint16_t last_s;
static uint16_t last_f;
static uint16_t viscous_count;
static uint16_t last_v;
static uint16_t seq;
static uint8_t def_kind;
static uint8_t def_due;
static uint16_t data_left;
static uint16_t tel_def_left;

static void encode_call(uint8_t *dest, const uint8_t *call, uint8_t ssid, uint8_t flags)
{
    uint8_t i;

    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        dest[i] = (uint8_t)(call[i] << 1);
    }
    dest[6] = (uint8_t)(0x60u | (uint8_t)((ssid & 0x0Fu) << 1) | flags);
}

static void put_byte(uint8_t byte)
{
    if (frame_n >= FRAME_MAX) {
        frame_ok = false;
        return;
    }
    frame[frame_n] = byte;
    ++frame_n;
}

static void put_text(const char *text)
{
    while (*text != '\0') {
        put_byte((uint8_t)*text);
        ++text;
    }
}

static void put_u16(uint16_t value)
{
    uint8_t text[5];
    uint8_t n = format_u16(value, 0u, text);
    uint8_t i;

    for (i = 0u; i < n; ++i) {
        put_byte(text[i]);
    }
}

static void put_seq(uint16_t value)
{
    uint8_t hundreds = 0u;
    uint8_t tens = 0u;

    while (value >= 100u) {
        value = (uint16_t)(value - 100u);
        ++hundreds;
    }
    while (value >= 10u) {
        value = (uint16_t)(value - 10u);
        ++tens;
    }
    put_byte((uint8_t)('0' + hundreds));
    put_byte((uint8_t)('0' + tens));
    put_byte((uint8_t)('0' + (uint8_t)value));
}

static bool call_blank(const uint8_t *call)
{
    uint8_t i;

    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        if (call[i] != (uint8_t)' ') {
            return false;
        }
    }
    return true;
}

static void start_frame(void)
{
    bool via = !call_blank(g_config.telpath);

    frame_n = 0u;
    frame_ok = true;
    encode_call(&frame[0], tncid, 0u, 0x80u);
    frame_n = 7u;
    encode_call(&frame[7], g_config.mycall, g_config.mycall_ssid, via ? 0u : 0x01u);
    frame_n = 14u;
    if (via) {
        encode_call(&frame[14], g_config.telpath, g_config.telpath_ssid, 0x01u);
        frame_n = 21u;
    }
    put_byte(AX25_UI);
    put_byte(AX25_PID);
}

/* :CALLSIGN : with the callsign padded to nine characters. SSID 0 is omitted. */
static void put_addressee(void)
{
    tnc2_callsign who;
    uint8_t i;

    format_tnc2_callsign(&who, g_config.mycall, g_config.mycall_ssid, (uint8_t)' ');
    put_byte((uint8_t)':');
    for (i = 0u; i < TNC2_CALLSIGN_LEN; ++i) {
        put_byte(who.text[i]);
    }
    put_byte((uint8_t)':');
}

static bool send_frame(bool once)
{
    if (!frame_ok || frame_n < 16u) {
        return false;
    }
    if (once) {
        // If this is due to a query, use the dupe check db to prevent
        // more than one set of definitions from being sent per 30 seconds.
        if (dupe_recent(frame, frame_n)) {
            return true;
        }
        dupe_remember(frame, frame_n);
    }
    return modem_send(frame, frame_n, g_config.txstale * 2u);
}

static bool send_message(const char *text, bool ident, bool once)
{
    uint8_t used = 0u;

    start_frame();
    put_addressee();
    while (*text != '\0' && used < MSG_MAX) {
        put_byte((uint8_t)*text);
        ++text;
        ++used;
    }
    if (ident && frame_ok && used < MSG_MAX) {
        uint8_t n = copy_version_platform(&frame[frame_n], (uint8_t)(MSG_MAX - used));

        frame_n = (uint16_t)(frame_n + n);
    }
    return send_frame(once);
}

static bool send_tel_def_kind(uint8_t kind, bool once)
{
    if (kind == 0u) {
        return send_message("BITS.11111100,WFDIGI ", true, once);
    }
    if (kind == 1u) {
        return send_message(msg_eqns, false, once);
    }
    if (kind == 2u) {
        return send_message(msg_parm, false, once);
    }
    return send_message(msg_unit, false, once);
}

static bool send_tel_def(void)
{
    return send_tel_def_kind(def_kind, false);
}

void telemetry_send_definitions(void)
{
    uint8_t kind;

    for (kind = 0u; kind < 4u; ++kind) {
        (void)send_tel_def_kind(kind, true);
    }
}

static void put_bit(bool on)
{
    put_byte(on ? (uint8_t)'1' : (uint8_t)'0');
}

/* len is the AX.25 frame without flags or CRC. Four more bytes count the
 * opening flag, the closing flag, and the CRC. The sum saturates at 65535.
 * Caller holds exclusive access to byte_count.
 */
static void add_bytes(uint16_t len)
{
    uint16_t add;

    if (len > (uint16_t)(65535u - 4u)) {
        byte_count = 65535u;
        return;
    }
    add = (uint16_t)(len + 4u);
    if (byte_count > (uint16_t)(65535u - add)) {
        byte_count = 65535u;
    } else {
        byte_count = (uint16_t)(byte_count + add);
    }
}

/* The report carried the high byte of snap. Keep the low byte, and any
 * bytes counted while the frame was queued.
 */
static void commit_byte_count(uint16_t snap)
{
    uint16_t high = (uint16_t)(snap & 0xFF00u);

    hardware_irq_off();
    if (byte_count >= high) {
        byte_count = (uint16_t)(byte_count - high);
    }
    hardware_irq_on();
}

static bool send_data(void)
{
    uint16_t rx_now;
    uint16_t tx_now;
    uint16_t rx;
    uint16_t tx;
    uint16_t r;
    uint16_t q;
    uint16_t s;
    uint16_t f;
    uint16_t dr;
    uint16_t dq;
    uint16_t ds;
    uint16_t df;
    uint16_t drops;
    uint16_t viscous;
    uint16_t bytes_now;

    hardware_irq_off();
    rx_now = rx_count;
    bytes_now = byte_count;
    hardware_irq_on();
    tx_now = tx_count;
    rx = (uint16_t)(rx_now - last_rx);
    tx = (uint16_t)(tx_now - last_tx);
    modem_drop_counts(&r, &q, &s, &f);
    dr = (uint16_t)(r - last_r);
    dq = (uint16_t)(q - last_q);
    ds = (uint16_t)(s - last_s);
    df = (uint16_t)(f - last_f);
    drops = dr;
    if (dq > (uint16_t)(65535u - drops)) {
        drops = 65535u;
    } else {
        drops = (uint16_t)(drops + dq);
    }
    if (ds > (uint16_t)(65535u - drops)) {
        drops = 65535u;
    } else {
        drops = (uint16_t)(drops + ds);
    }
    if (df > (uint16_t)(65535u - drops)) {
        drops = 65535u;
    } else {
        drops = (uint16_t)(drops + df);
    }
    viscous = (uint16_t)(viscous_count - last_v);

    start_frame();
    put_byte((uint8_t)'T');
    put_byte((uint8_t)'#');
    put_seq(seq);
    put_byte((uint8_t)',');
    put_u16(rx);
    put_byte((uint8_t)',');
    put_u16(tx);
    put_byte((uint8_t)',');
    put_u16(viscous);
    put_byte((uint8_t)',');
    put_u16(drops);
    put_byte((uint8_t)',');
    put_u16((uint16_t)(bytes_now >> 8));
    put_byte((uint8_t)',');
    put_bit(cold_boot);
    put_bit(rom_checksum_pass);
    put_bit(df != 0u);
    put_bit(dq != 0u);
    put_bit(dr != 0u);
    put_bit(ds != 0u);
    put_bit(false);
    put_bit(false);
    if (!send_frame(false)) {
        return false;
    }

    last_rx = rx_now;
    last_tx = tx_now;
    last_r = r;
    last_q = q;
    last_s = s;
    last_f = f;
    last_v = viscous_count;
    commit_byte_count(bytes_now);
    if (seq >= 999u) {
        seq = 0u;
    } else {
        ++seq;
    }
    return true;
}

static void tel_def_sent(void)
{
    def_due = 0u;
    def_kind = (uint8_t)((def_kind + 1u) & 3u);
    tel_def_left = TELEMETRY_DEF_INTERVAL;
}

static void arm_schedule(void)
{
    def_kind = 0u;
    if (g_config.telemetry == 0u) {
        def_due = 0u;
        tel_def_left = 0u;
        data_left = 0u;
        return;
    }
    def_due = 0u;
    tel_def_left = TELEMETRY_START_DELAY;
    data_left = SLOT_SECONDS;
}

void telemetry_note_rx(uint16_t len)
{
    ++rx_count;
    add_bytes(len);
}

void telemetry_note_tx(uint16_t len)
{
    ++tx_count;
    hardware_irq_off();
    add_bytes(len);
    hardware_irq_on();
}

void telemetry_note_viscous(void)
{
    ++viscous_count;
}

void telemetry_packet_counts(uint16_t *received, uint16_t *transmitted)
{
    uint16_t rx_now;

    hardware_irq_off();
    rx_now = rx_count;
    hardware_irq_restore();
    *received = (uint16_t)(rx_now - last_rx);
    *transmitted = (uint16_t)(tx_count - last_tx);
}

void telemetry_init(void)
{
    hardware_irq_off();
    rx_count = 0u;
    byte_count = 0u;
    hardware_irq_restore();
    tx_count = 0u;
    last_rx = 0u;
    last_tx = 0u;
    last_r = 0u;
    last_q = 0u;
    last_s = 0u;
    last_f = 0u;
    viscous_count = 0u;
    last_v = 0u;
    seq = 0u;
    arm_schedule();
}

void telemetry_restart(void)
{
    uint16_t r;
    uint16_t q;
    uint16_t s;
    uint16_t f;

    hardware_irq_off();
    last_rx = rx_count;
    byte_count = 0u;
    hardware_irq_restore();
    last_tx = tx_count;
    modem_drop_counts(&r, &q, &s, &f);
    last_r = r;
    last_q = q;
    last_s = s;
    last_f = f;
    last_v = viscous_count;
    seq = 0u;
    arm_schedule();
}

void telemetry_second(void)
{
    if (g_config.telemetry == 0u) {
        return;
    }
    if (def_due == 0u && tel_def_left != 0u) {
        --tel_def_left;
        if (tel_def_left == 0u) {
            def_due = 1u;
        }
    }
    if (def_due != 0u && !tx_interlock && send_tel_def()) {
        tel_def_sent();
    }
    if (data_left != 0u) {
        --data_left;
    }
    if (data_left == 0u && !tx_interlock && send_data()) {
        data_left = SLOT_SECONDS;
    }
}
