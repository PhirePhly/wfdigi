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
 */
#define FRAME_MAX 128u
#define MSG_MAX 67u
#define SLOT_SECONDS 600u
#define TELEMETRY_START_DELAY 300u
#define TELEMETRY_DEF_INTERVAL 14400u

static const char msg_eqns[] = "EQNS.0,0.1,0,0,0.1,0,0,0.1,0,0,1,0,0,1,0";
static const char msg_parm[] = "PARM.RxPkts,TxPks,ViscousDrops,AdverseDrops,,COLD,SUM,!F,!Q,!R,!S";
static const char msg_unit[] = "UNIT.pkts/min,pkts/min,pkts/min,count,";

_Static_assert(sizeof(msg_eqns) - 1u <= MSG_MAX, "EQNS message is too long");
_Static_assert(sizeof(msg_parm) - 1u <= MSG_MAX, "PARM message is too long");
_Static_assert(sizeof(msg_unit) - 1u <= MSG_MAX, "UNIT message is too long");

static uint8_t frame[FRAME_MAX];
static uint16_t frame_n;
static bool frame_ok;

static volatile uint16_t rx_count;
static uint16_t tx_count;
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
    uint8_t raw[9];
    uint8_t n = 0u;
    uint8_t i;
    uint8_t ssid = g_config.mycall_ssid;

    for (i = 0u; i < 9u; ++i) {
        raw[i] = (uint8_t)' ';
    }
    for (i = 0u; i < CALLSIGN_LEN && g_config.mycall[i] != (uint8_t)' ' && n < 9u; ++i) {
        raw[n] = g_config.mycall[i];
        ++n;
    }
    if (ssid != 0u && n < 9u) {
        raw[n] = (uint8_t)'-';
        ++n;
        if (ssid >= 10u && n < 9u) {
            raw[n] = (uint8_t)'1';
            ++n;
            ssid = (uint8_t)(ssid - 10u);
        }
        if (n < 9u) {
            raw[n] = (uint8_t)('0' + ssid);
        }
    }
    put_byte((uint8_t)':');
    for (i = 0u; i < 9u; ++i) {
        put_byte(raw[i]);
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

static bool send_message(const char *text, const char *extra, bool once)
{
    uint8_t used = 0u;

    start_frame();
    put_addressee();
    while (*text != '\0' && used < MSG_MAX) {
        put_byte((uint8_t)*text);
        ++text;
        ++used;
    }
    if (extra != 0) {
        while (*extra != '\0' && used < MSG_MAX) {
            put_byte((uint8_t)*extra);
            ++extra;
            ++used;
        }
    }
    return send_frame(once);
}

static bool send_tel_def_kind(uint8_t kind, bool once)
{
    if (kind == 0u) {
        return send_message("BITS.11111100,WFDIGI ", wfdigi_version, once);
    }
    if (kind == 1u) {
        return send_message(msg_eqns, 0, once);
    }
    if (kind == 2u) {
        return send_message(msg_parm, 0, once);
    }
    return send_message(msg_unit, 0, once);
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

    hardware_irq_off();
    rx_now = rx_count;
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
    put_text(",0,");
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

void telemetry_note_rx(void)
{
    ++rx_count;
}

void telemetry_note_tx(void)
{
    ++tx_count;
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
