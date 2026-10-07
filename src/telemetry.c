#include "hardware.h"

/* APRS telemetry. Received and transmitted totals live in working RAM, count
 * whether or not telemetry is on, and start over on every boot. They are
 * 16-bit and roll over. A UI frame every 10 minutes carries the unsigned
 * difference since the previous report. TELPATH is its digipeater; a blank
 * path is sent direct. One definition message goes out each hour: BITS,
 * EQNS, PARM, then UNIT.
 */
#define FRAME_MAX 128u
#define AX25_UI 0x03u
#define AX25_PID 0xF0u
#define MSG_MAX 67u
#define SLOT_SECONDS 600u
#define TELEMETRY_DEF_INTERVAL 3600u

static const char msg_eqns[] = "EQNS.0,0.1,0,0,0.1,0,0,1,0,0,1,0,0,1,0";
static const char msg_parm[] = "PARM.ReceivePkts,TransmitPks,AdverseDrops,,";
static const char msg_unit[] = "UNIT.pkts/min,pkts/min,count,,";

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

static bool send_frame(void)
{
    if (!frame_ok || frame_n < 16u) {
        return false;
    }
    return modem_send(frame, frame_n);
}

static bool send_message(const char *text, const char *extra)
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
    return send_frame();
}

static bool send_tel_def(void)
{
    if (def_kind == 0u) {
        return send_message("BITS.11111111,WFDIGI ", wfdigi_version);
    }
    if (def_kind == 1u) {
        return send_message(msg_eqns, 0);
    }
    if (def_kind == 2u) {
        return send_message(msg_parm, 0);
    }
    return send_message(msg_unit, 0);
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
    uint16_t dr;
    uint16_t dq;
    uint16_t ds;
    uint16_t drops;

    hardware_irq_off();
    rx_now = rx_count;
    hardware_irq_on();
    tx_now = tx_count;
    rx = (uint16_t)(rx_now - last_rx);
    tx = (uint16_t)(tx_now - last_tx);
    modem_drop_counts(&r, &q, &s);
    dr = (uint16_t)(r - last_r);
    dq = (uint16_t)(q - last_q);
    ds = (uint16_t)(s - last_s);
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

    start_frame();
    put_byte((uint8_t)'T');
    put_byte((uint8_t)'#');
    put_seq(seq);
    put_byte((uint8_t)',');
    put_u16(rx);
    put_byte((uint8_t)',');
    put_u16(tx);
    put_byte((uint8_t)',');
    put_u16(drops);
    put_text(",0,0,00000000");
    if (!send_frame()) {
        return false;
    }

    last_rx = rx_now;
    last_tx = tx_now;
    last_r = r;
    last_q = q;
    last_s = s;
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

void telemetry_note_rx(void)
{
    ++rx_count;
}

void telemetry_note_tx(void)
{
    ++tx_count;
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
    seq = 0u;
    def_kind = 0u;
    tel_def_left = 0u;
    if (g_config.telemetry == 0u) {
        def_due = 0u;
        data_left = 0u;
        return;
    }
    def_due = 1u;
    data_left = SLOT_SECONDS;
}

void telemetry_restart(void)
{
    uint16_t r;
    uint16_t q;
    uint16_t s;

    hardware_irq_off();
    last_rx = rx_count;
    hardware_irq_restore();
    last_tx = tx_count;
    modem_drop_counts(&r, &q, &s);
    last_r = r;
    last_q = q;
    last_s = s;
    seq = 0u;
    def_kind = 0u;
    tel_def_left = 0u;
    if (g_config.telemetry == 0u) {
        def_due = 0u;
        data_left = 0u;
        return;
    }
    def_due = 1u;
    data_left = SLOT_SECONDS;
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
