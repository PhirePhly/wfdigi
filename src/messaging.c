#include "hardware.h"

/* Directed APRS messages and queries, and one-off UI frames from the terminal.
 * A message for MYCALL is consumed. A general ?APRS? may shorten the beacon
 * countdown and is then left for the digipeater, so this file never records a
 * received frame.
 */
#define FRAME_MAX 128u
#define MSG_ID_MAX 5u
/* Longest APRS message text, after the addressee. */
#define MSG_TEXT_MAX 67u

static uint8_t frame[FRAME_MAX];
static uint8_t reply_pad[FRAME_MAX];

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

static void encode_call(uint8_t *dest, const uint8_t *call, uint8_t ssid, uint8_t flags)
{
    uint8_t i;

    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        dest[i] = (uint8_t)(call[i] << 1);
    }
    dest[6] = (uint8_t)(0x60u | (uint8_t)((ssid & 0x0Fu) << 1) | flags);
}

static bool same_addressee(const uint8_t *got, const tnc2_callsign *want)
{
    uint8_t i;

    for (i = 0u; i < TNC2_CALLSIGN_LEN; ++i) {
        if (letter_to_upper(got[i]) != want->text[i]) {
            return false;
        }
    }
    return true;
}

static bool mine(const uint8_t *addressee)
{
    tnc2_callsign want;

    format_tnc2_callsign(&want, g_config.mycall, g_config.mycall_ssid, (uint8_t)' ');
    return same_addressee(addressee, &want);
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
    uint8_t msg_id_len;
    uint16_t at = len;
    uint16_t n;

    *body_len = len;
    *id = 0;
    *id_len = 0u;
    // Scan for the last { in the text
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
    for (msg_id_len = 0u; msg_id_len < n; ++msg_id_len) {
        if (!is_alphanumeric(text[(uint16_t)(at + 1u + msg_id_len)])) {
            break;
        }
    }
    if (msg_id_len == 0u || msg_id_len > MSG_ID_MAX) {
        return false;
    }
    *body_len = at;
    *id = text + (at + 1u);
    *id_len = (uint8_t)msg_id_len;
    return true;
}

/* :MYCALL:?APRSx, optionally followed by a space and an argument. */
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
    if (*letter == (uint8_t)'?') {
        return true;
    }
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

/* One delay in 0..GENERAL_QUERY_WINDOW. Two spans fit in a byte, so the rest
 * of the draw is discarded and the upper span folds back by subtraction.
 */
static uint16_t general_query_delay(void)
{
    uint8_t span = (uint8_t)(GENERAL_QUERY_WINDOW + 1u);
    uint8_t limit = (uint8_t)(span + span);
    uint8_t draw;

    prng_stir(timer_seconds());
    do {
        draw = prng_u8();
    } while (draw >= limit);
    if (draw >= span) {
        draw = (uint8_t)(draw - span);
    }
    return draw;
}

/* The first general query in a beacon cycle picks the delay and marks the
 * cycle. A later copy must not pick again. The mark clears when the
 * countdown reaches zero.
 */
static void general_query(void)
{
    uint16_t delay;
    uint16_t left;

    if (g_config.beacon_every == 0u || timer_beacon_accelerated()) {
        return;
    }
    delay = general_query_delay();
    left = timer_beacon_left();
    if (delay < left) {
        left = delay;
    }
    timer_beacon_set(left);
    timer_beacon_accelerate();
}

/* MYCALL to the platform TNCID. The first beacon path is the digipeater when it is set.
 * Returns the index of the information field.
 */
static uint16_t begin_ui(void)
{
    uint16_t n;
    bool via;

    via = g_config.bpath_count != 0u && !call_blank(g_config.bpath[0]);
    encode_call(&frame[0], tncid, 0u, 0x80u);
    n = 7u;
    encode_call(&frame[7], g_config.mycall, g_config.mycall_ssid, via ? 0u : 0x01u);
    n = 14u;
    if (via) {
        encode_call(&frame[14], g_config.bpath[0], g_config.bpath_ssid[0], 0x01u);
        n = 21u;
    }
    frame[n] = AX25_UI;
    ++n;
    frame[n] = AX25_PID;
    ++n;
    return n;
}

static void send_reply(const tnc2_callsign *to, const uint8_t *text, uint8_t text_len)
{
    uint16_t n;
    uint8_t i;

    n = begin_ui();
    frame[n] = (uint8_t)':';
    ++n;
    for (i = 0u; i < TNC2_CALLSIGN_LEN; ++i) {
        frame[n] = to->text[i];
        ++n;
    }
    frame[n] = (uint8_t)':';
    ++n;
    for (i = 0u; i < text_len; ++i) {
        if (n >= FRAME_MAX) {
            return;
        }
        frame[n] = text[i];
        ++n;
    }
    // Record the reply before queueing it. A later copy of an identical reply
    // is an answer to an echo of the original question.
    if (dupe_recent(frame, n)) {
        return;
    }
    dupe_remember(frame, n);
    (void)modem_send(frame, n, g_config.txstale * 2u);
}

/* 0 when queued, 1 when the text does not fit, 2 when the transmit queue is full. */
uint8_t messaging_send_raw(const char *info)
{
    uint16_t n;

    n = begin_ui();
    while (*info != '\0') {
        if (n >= FRAME_MAX) {
            return 1u;
        }
        frame[n] = (uint8_t)*info;
        ++n;
        ++info;
    }
    if (!modem_send(frame, n, g_config.txstale)) {
        return 2u;
    }
    return 0u;
}

static void send_ack(const tnc2_callsign *to, const uint8_t *id, uint8_t id_len)
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
    send_reply(to, text, n);
}

static void send_unsupported(const tnc2_callsign *to)
{
    static const char text[] = "ERR - query type unsupported";
    uint8_t i;

    for (i = 0u; i < (uint8_t)(sizeof text - 1u); ++i) {
        reply_pad[i] = (uint8_t)text[i];
    }
    send_reply(to, reply_pad, (uint8_t)(sizeof text - 1u));
}

static void send_supported_queries(const tnc2_callsign *to)
{
    static const char text[] = " ?APRSD ?APRSP ?APRSU ?APRSV";
    uint8_t i;

    for (i = 0u; i < (uint8_t)(sizeof text - 1u); ++i) {
        reply_pad[i] = (uint8_t)text[i];
    }
    send_reply(to, reply_pad, (uint8_t)(sizeof text - 1u));
}

/* The firmware version, a space, then the platform name, as in "v0.2 pk88". */
static void send_version_string(const tnc2_callsign *to)
{
    uint8_t n = 0u;
    uint8_t i = 0u;

    while (wfdigi_version[i] != '\0' && n < FRAME_MAX) {
        reply_pad[n] = (uint8_t)wfdigi_version[i];
        ++n;
        ++i;
    }
    if (n < FRAME_MAX) {
        reply_pad[n] = (uint8_t)' ';
        ++n;
    }
    i = 0u;
    while (wfdigi_platform[i] != '\0' && n < FRAME_MAX) {
        reply_pad[n] = (uint8_t)wfdigi_platform[i];
        ++n;
        ++i;
    }
    send_reply(to, reply_pad, n);
}

/* Directs= and up to HEARD_PAGE stations, in the order they sit in the heard
 * table. A longer list continues in another message.
 */
static void send_directs(const tnc2_callsign *to)
{
    static const char prefix[] = "Directs= ";
    const void *from = 0;
    uint8_t pages = 0u;

    do {
        uint8_t n;
        uint16_t got;

        for (n = 0u; n < (uint8_t)(sizeof prefix - 1u); ++n) {
            reply_pad[n] = (uint8_t)prefix[n];
        }
        got = heard_list(reply_pad + n, (uint16_t)(MSG_TEXT_MAX - n), &from);
        n = (uint8_t)(n + got);
        send_reply(to, reply_pad, n);
        ++pages;
    } while (from != 0 && pages < HEARD_SLOTS);
}

static void answer_directed(const tnc2_callsign *from, const uint8_t *text, uint16_t len)
{
    const uint8_t *id = 0;
    uint8_t id_len = 0u;
    uint8_t letter = 0u;
    uint16_t body_len = len;

    if (take_message_id(text, len, &body_len, &id, &id_len)) {
        send_ack(from, id, id_len);
    }
    if (!query_letter(text, body_len, &letter)) {
        return;
    }
    switch (letter) {
    case (uint8_t)'?':
        send_supported_queries(from);
        break;
    case (uint8_t)'D':
        send_directs(from);
        break;
    case (uint8_t)'P':
        query_beacon();
        break;
    case (uint8_t)'U':
        telemetry_send_definitions();
        break;
    case (uint8_t)'V':
        send_version_string(from);
        break;
    default:
        send_unsupported(from);
        break;
    }
}

static bool ui_payload(const uint8_t *rx, uint16_t len, tnc2_callsign *from, const uint8_t **info,
                       uint16_t *info_len)
{
    uint16_t at = 0u;
    uint8_t n = 0u;
    uint8_t i;
    uint8_t call[CALLSIGN_LEN];
    const uint8_t *src;

    if (rx == 0 || len < (uint16_t)(AX25_ADDR + AX25_ADDR + 2u)) {
        return false;
    }
    while (n < AX25_MAX_ADDRS && (uint16_t)(at + AX25_ADDR) <= len) {
        uint8_t ext = (uint8_t)(rx[(uint16_t)(at + 6u)] & 0x01u);

        at = (uint16_t)(at + AX25_ADDR);
        ++n;
        if (ext != 0u) {
            break;
        }
    }
    if (n < 2u || (rx[(uint16_t)(at - 1u)] & 0x01u) == 0u) {
        return false;
    }
    if ((uint16_t)(at + 2u) > len || (rx[at] & 0xEFu) != AX25_UI || rx[(uint16_t)(at + 1u)] != AX25_PID) {
        return false;
    }
    src = rx + AX25_ADDR;
    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        call[i] = (uint8_t)(src[i] >> 1);
    }
    format_tnc2_callsign(from, call, (uint8_t)((src[6] >> 1) & 0x0Fu), (uint8_t)' ');
    *info = rx + (uint16_t)(at + 2u);
    *info_len = (uint16_t)(len - (uint16_t)(at + 2u));
    return true;
}

/* }CALL>DEST,PATH:payload. CALL is copied as TNC2 text and padded with spaces.
 * The originator replaces the AX.25 source.
 */
static bool third_party(const uint8_t **info, uint16_t *info_len, tnc2_callsign *from)
{
    const uint8_t *raw = *info;
    uint16_t len = *info_len;
    uint16_t raw_index = 1u;
    uint8_t n;

    // Are we a third party message with a payload?
    if (len < 2u || raw[0] != (uint8_t)'}') {
        return false;
    }
    // Clear the destination callsign.
    for (n = 0u; n < TNC2_CALLSIGN_LEN; ++n) {
        from->text[n] = (uint8_t)' ';
    }
    // Copy the originator callsign.
    n = 0u;
    while (raw_index < len && raw[raw_index] != (uint8_t)'>') {
        if (n == TNC2_CALLSIGN_LEN) {
            return false;
        }
        from->text[n] = letter_to_upper(raw[raw_index]);
        ++n;
        ++raw_index;
    }
    // This is overly permissive allowing 1-2 character callsigns.
    if (n == 0u || raw_index >= len) {
        return false;
    }
    // Scan to the third party payload
    while (raw_index < len && raw[raw_index] != (uint8_t)':') {
        ++raw_index;
    }
    if (raw_index >= len || raw[raw_index] != (uint8_t)':') {
        return false;
    }
    ++raw_index;
    *info = raw + raw_index;
    *info_len = (uint16_t)(len - raw_index);
    return true;
}

// Returns true if the frame was successfully disposed of for messaging.
// Returns false if the frame should still be considered for digipeating.
bool process_frame_for_messaging(const uint8_t *rx, uint16_t len)
{
    tnc2_callsign from;
    const uint8_t *info;
    uint16_t info_len;
    bool wrapped;

    if (!ui_payload(rx, len, &from, &info, &info_len)) {
        return false;
    }
    wrapped = info_len != 0u && info[0] == (uint8_t)'}';
    if (wrapped && !third_party(&info, &info_len, &from)) {
        return false;
    }
    if (same_text(info, info_len, "?APRS?")) {
        general_query();
        return false;
    }
    if (info_len < (uint16_t)(TNC2_CALLSIGN_LEN + 2u) || info[0] != (uint8_t)':' ||
        info[TNC2_CALLSIGN_LEN + 1u] != (uint8_t)':') {
        return false;
    }
    if (!mine(info + 1u)) {
        return false;
    }
    answer_directed(&from, info + (uint16_t)(TNC2_CALLSIGN_LEN + 2u),
                    (uint16_t)(info_len - (uint16_t)(TNC2_CALLSIGN_LEN + 2u)));
    return true;
}
