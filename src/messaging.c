#include "hardware.h"

/* Directed APRS messages and queries. A message for MYCALL is consumed.
 * A general ?APRS? may start the normal beacon and is then left for the
 * digipeater, so this file never records a received frame.
 */
#define AX25_ADDR 7u
#define AX25_MAX_ADDRS 10u
#define AX25_UI 0x03u
#define AX25_PID 0xF0u
#define FRAME_MAX 128u
#define ADDRESSEE_LEN 9u
#define MSG_ID_MAX 5u

static uint8_t reply[FRAME_MAX];

void messaging_init(void)
{
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

static bool alnum(uint8_t c)
{
    if (c >= (uint8_t)'0' && c <= (uint8_t)'9') {
        return true;
    }
    c = letter_to_upper(c);
    return c >= (uint8_t)'A' && c <= (uint8_t)'Z';
}

static void encode_call(uint8_t *dest, const uint8_t *call, uint8_t ssid, uint8_t flags)
{
    uint8_t i;

    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        dest[i] = (uint8_t)(call[i] << 1);
    }
    dest[6] = (uint8_t)(0x60u | (uint8_t)((ssid & 0x0Fu) << 1) | flags);
}

/* :CALLSIGN : uses nine characters. SSID 0 is omitted. */
static void format_addressee(const uint8_t *call, uint8_t ssid, uint8_t dest[ADDRESSEE_LEN])
{
    uint8_t n = 0u;
    uint8_t i;

    for (i = 0u; i < ADDRESSEE_LEN; ++i) {
        dest[i] = (uint8_t)' ';
    }
    for (i = 0u; i < CALLSIGN_LEN && call[i] != (uint8_t)' ' && n < ADDRESSEE_LEN; ++i) {
        dest[n] = letter_to_upper(call[i]);
        ++n;
    }
    if (ssid == 0u || n >= ADDRESSEE_LEN) {
        return;
    }
    dest[n] = (uint8_t)'-';
    ++n;
    if (ssid >= 10u && n < ADDRESSEE_LEN) {
        dest[n] = (uint8_t)'1';
        ++n;
        ssid = (uint8_t)(ssid - 10u);
    }
    if (n < ADDRESSEE_LEN) {
        dest[n] = (uint8_t)('0' + ssid);
    }
}

static bool same_addressee(const uint8_t *got, const uint8_t *want)
{
    uint8_t i;

    for (i = 0u; i < ADDRESSEE_LEN; ++i) {
        if (letter_to_upper(got[i]) != want[i]) {
            return false;
        }
    }
    return true;
}

static bool mine(const uint8_t *addressee)
{
    uint8_t want[ADDRESSEE_LEN];

    format_addressee(g_config.mycall, g_config.mycall_ssid, want);
    return same_addressee(addressee, want);
}

/* lit is the first word: it matches through the end of text or a space. */
static bool same_text(const uint8_t *text, uint16_t len, const char *lit)
{
    uint16_t i = 0u;

    while (lit[i] != '\0') {
        if (i >= len || text[i] != (uint8_t)lit[i]) {
            return false;
        }
        ++i;
    }
    return i == len || text[i] == (uint8_t)' ';
}

/* The message number is the last { plus 1-5 alphanumeric characters. */
static bool take_message_id(const uint8_t *text, uint16_t len, uint16_t *body_len,
                            const uint8_t **id, uint8_t *id_len)
{
    uint16_t i;
    uint16_t at = len;
    uint16_t n;

    *body_len = len;
    *id = 0;
    *id_len = 0u;
    for (i = 0u; i < len; ++i) {
        if (text[i] == (uint8_t)'{') {
            at = i;
        }
    }
    if (at == len) {
        return false;
    }
    n = (uint16_t)(len - (uint16_t)(at + 1u));
    if (n < 1u || n > MSG_ID_MAX) {
        return false;
    }
    for (i = 0u; i < n; ++i) {
        if (!alnum(text[(uint16_t)(at + 1u + i)])) {
            return false;
        }
    }
    *body_len = at;
    *id = text + (at + 1u);
    *id_len = (uint8_t)n;
    return true;
}

/* :MYCALL:?APRSX, optionally followed by a space and an argument. */
static bool query_letter(const uint8_t *text, uint16_t len, uint8_t *letter)
{
    char prefix[] = "?APRS";

    if (len < 6u) {
        return false;
    }
    if (!same_text(text, 5u, prefix)) {
        return false;
    }
    *letter = text[5];
    if (*letter < (uint8_t)'A' || *letter > (uint8_t)'Z') {
        return false;
    }
    return true;
}

static void query_beacon(void)
{
    if (timer_query_cooling()) {
        return;
    }
    if (!timer_beacon_now()) {
        return;
    }
    timer_start_query_cooldown();
}

/* Record the reply before queueing it. A later copy of the same reply is an echo. */
static void send_reply(const uint8_t *to_call, uint8_t to_ssid, const uint8_t *text, uint8_t text_len)
{
    uint8_t who[ADDRESSEE_LEN];
    uint16_t n = 0u;
    uint8_t i;
    bool via;

    via = g_config.bpath_count != 0u && !call_blank(g_config.bpath[0]);
    encode_call(&reply[0], tncid, 0u, 0x80u);
    n = 7u;
    encode_call(&reply[7], g_config.mycall, g_config.mycall_ssid, via ? 0u : 0x01u);
    n = 14u;
    if (via) {
        encode_call(&reply[14], g_config.bpath[0], g_config.bpath_ssid[0], 0x01u);
        n = 21u;
    }
    reply[n] = AX25_UI;
    ++n;
    reply[n] = AX25_PID;
    ++n;
    reply[n] = (uint8_t)':';
    ++n;
    format_addressee(to_call, to_ssid, who);
    for (i = 0u; i < ADDRESSEE_LEN; ++i) {
        reply[n] = who[i];
        ++n;
    }
    reply[n] = (uint8_t)':';
    ++n;
    for (i = 0u; i < text_len; ++i) {
        if (n >= FRAME_MAX) {
            return;
        }
        reply[n] = text[i];
        ++n;
    }
    if (dupe_recent(reply, n)) {
        return;
    }
    dupe_remember(reply, n);
    (void)modem_send(reply, n, TX_EXPIRE_SECONDS);
}

static void send_ack(const uint8_t *to_call, uint8_t to_ssid, const uint8_t *id, uint8_t id_len)
{
    uint8_t text[3u + MSG_ID_MAX];
    uint8_t n = 0u;
    uint8_t i;

    text[n] = (uint8_t)'a';
    ++n;
    text[n] = (uint8_t)'c';
    ++n;
    text[n] = (uint8_t)'k';
    ++n;
    for (i = 0u; i < id_len; ++i) {
        text[n] = id[i];
        ++n;
    }
    send_reply(to_call, to_ssid, text, n);
}

static void send_unsupported(const uint8_t *to_call, uint8_t to_ssid)
{
    static const char text[] = "ERR - query type unsupported";
    uint8_t raw[sizeof text - 1u];
    uint8_t i;

    for (i = 0u; i < (uint8_t)(sizeof text - 1u); ++i) {
        raw[i] = (uint8_t)text[i];
    }
    send_reply(to_call, to_ssid, raw, (uint8_t)(sizeof text - 1u));
}

static void answer_directed(const uint8_t *from, uint8_t from_ssid, const uint8_t *text, uint16_t len)
{
    const uint8_t *id = 0;
    uint8_t id_len = 0u;
    uint8_t letter = 0u;
    uint16_t body_len = len;

    if (take_message_id(text, len, &body_len, &id, &id_len)) {
        send_ack(from, from_ssid, id, id_len);
    }
    if (!query_letter(text, body_len, &letter)) {
        return;
    }
    switch (letter) {
    case (uint8_t)'P':
        query_beacon();
        break;
    case (uint8_t)'U':
        telemetry_send_definitions();
        break;
    default:
        send_unsupported(from, from_ssid);
        break;
    }
}

static bool ui_payload(const uint8_t *frame, uint16_t len, uint8_t from[CALLSIGN_LEN], uint8_t *from_ssid,
                       const uint8_t **info, uint16_t *info_len)
{
    uint16_t at = 0u;
    uint8_t n = 0u;
    uint8_t i;
    const uint8_t *src;

    if (frame == 0 || len < (uint16_t)(AX25_ADDR + AX25_ADDR + 2u)) {
        return false;
    }
    while (n < AX25_MAX_ADDRS && (uint16_t)(at + AX25_ADDR) <= len) {
        uint8_t ext = (uint8_t)(frame[(uint16_t)(at + 6u)] & 0x01u);

        at = (uint16_t)(at + AX25_ADDR);
        ++n;
        if (ext != 0u) {
            break;
        }
    }
    if (n < 2u || (frame[(uint16_t)(at - 1u)] & 0x01u) == 0u) {
        return false;
    }
    if ((uint16_t)(at + 2u) > len || (frame[at] & 0xEFu) != AX25_UI || frame[(uint16_t)(at + 1u)] != AX25_PID) {
        return false;
    }
    src = frame + AX25_ADDR;
    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        from[i] = (uint8_t)(src[i] >> 1);
    }
    *from_ssid = (uint8_t)((src[6] >> 1) & 0x0Fu);
    *info = frame + (uint16_t)(at + 2u);
    *info_len = (uint16_t)(len - (uint16_t)(at + 2u));
    return true;
}

/* }CALL>DEST,PATH:payload. The originator replaces the AX.25 source. */
static bool third_party(const uint8_t **info, uint16_t *info_len, uint8_t from[CALLSIGN_LEN], uint8_t *from_ssid)
{
    const uint8_t *raw = *info;
    uint16_t len = *info_len;
    uint16_t i = 1u;
    uint8_t n = 0u;
    uint8_t ssid = 0u;

    if (len < 2u || raw[0] != (uint8_t)'}') {
        return false;
    }
    for (n = 0u; n < CALLSIGN_LEN; ++n) {
        from[n] = (uint8_t)' ';
    }
    n = 0u;
    while (i < len && alnum(raw[i]) && n < CALLSIGN_LEN) {
        from[n] = letter_to_upper(raw[i]);
        ++n;
        ++i;
    }
    if (n == 0u) {
        return false;
    }
    if (i < len && raw[i] == (uint8_t)'-') {
        uint8_t digits = 0u;

        ++i;
        while (i < len && raw[i] >= (uint8_t)'0' && raw[i] <= (uint8_t)'9') {
            ssid = (uint8_t)((uint8_t)(ssid * 10u) + (uint8_t)(raw[i] - (uint8_t)'0'));
            ++digits;
            ++i;
            if (digits > 2u) {
                return false;
            }
        }
        if (digits == 0u || ssid > 15u) {
            return false;
        }
    }
    if (i >= len || raw[i] != (uint8_t)'>') {
        return false;
    }
    ++i;
    while (i < len && raw[i] != (uint8_t)':' && raw[i] != (uint8_t)',') {
        ++i;
    }
    while (i < len && raw[i] == (uint8_t)',') {
        ++i;
        while (i < len && raw[i] != (uint8_t)':' && raw[i] != (uint8_t)',') {
            ++i;
        }
    }
    if (i >= len || raw[i] != (uint8_t)':') {
        return false;
    }
    ++i;
    *from_ssid = ssid;
    *info = raw + i;
    *info_len = (uint16_t)(len - i);
    return true;
}

// Returns true if the frame was successfully disposed of for messaging.
// Returns false if the frame should still be considered for digipeating.
bool process_frame_for_messaging(const uint8_t *frame, uint16_t len)
{
    uint8_t from[CALLSIGN_LEN];
    uint8_t from_ssid = 0u;
    const uint8_t *info;
    uint16_t info_len;
    bool wrapped;

    if (!ui_payload(frame, len, from, &from_ssid, &info, &info_len)) {
        return false;
    }
    wrapped = info_len != 0u && info[0] == (uint8_t)'}';
    if (wrapped && !third_party(&info, &info_len, from, &from_ssid)) {
        return false;
    }
    if (same_text(info, info_len, "?APRS?")) {
        query_beacon();
        return false;
    }
    if (info_len < (uint16_t)(ADDRESSEE_LEN + 2u) || info[0] != (uint8_t)':' ||
        info[ADDRESSEE_LEN + 1u] != (uint8_t)':') {
        return false;
    }
    if (!mine(info + 1u)) {
        return false;
    }
    answer_directed(from, from_ssid, info + (uint16_t)(ADDRESSEE_LEN + 2u),
                    (uint16_t)(info_len - (uint16_t)(ADDRESSEE_LEN + 2u)));
    return true;
}
