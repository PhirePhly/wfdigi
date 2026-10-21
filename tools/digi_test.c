/* Host checks for the digipeater in src/digi.c and src/dupe.c.
 *
 * Frames use terminal order. Every repeated digipeater is marked, not only the
 * last one: N0SRC>APRS,AAA*,WFDIGI*,WIDE2-1:Hi
 * SSID 0 is omitted. A star is the has-been-repeated bit. Payload escapes are
 * \r, \n, \\, and \xNN. A null expected frame means nothing is transmitted.
 *
 * Ordinary cases start from an empty duplicate list. The duplicate tests keep
 * that list and move the clock themselves.
 */

#include "wfdigi.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define FRAME_CAP AX25_MAX
#define TEXT_CAP 800
#define TX_KEEP 4

DigiConfig g_config;

static uint16_t now;
static uint8_t tx_frame[TX_KEEP][FRAME_CAP];
static uint16_t tx_len_at[TX_KEEP];
static uint16_t tx_expire_at[TX_KEEP];
static int tx_count;
static int viscous_log_count;
static int viscous_note_count;
static int format_drops;
static uint8_t random_draw[8];
static uint8_t random_count;
static uint8_t random_at;
static int passed;
static int failed;
static int beacon_now_count;
static int tel_def_count;
static uint8_t query_cool_left;

const uint8_t tncid[CALLSIGN_LEN] = {'A', 'P', 'W', 'F', 'D', 'P'};
const char wfdigi_version[] = "v9.9-test";
const char wfdigi_platform[] = "pk88";

uint16_t timer_seconds(void)
{
    return now;
}

bool timer_in_dupe_window(uint16_t heard_at)
{
    return (uint16_t)(now - heard_at) < DUPE_WINDOW;
}

void prng_stir(uint16_t extra)
{
    (void)extra;
}

uint8_t prng_u8(void)
{
    if (random_at < random_count) {
        uint8_t value = random_draw[random_at];

        ++random_at;
        return value;
    }
    return 0u;
}

void serial_putc(uint8_t byte)
{
    (void)byte;
}

bool timer_beacon_now(void)
{
    ++beacon_now_count;
    return true;
}

void telemetry_send_definitions(void)
{
    ++tel_def_count;
}

void timer_start_query_cooldown(void)
{
    query_cool_left = (uint8_t)DUPE_WINDOW;
}

bool timer_query_cooling(void)
{
    return query_cool_left != 0u;
}

/* The host clock is assigned, not ticked, so tests step this countdown themselves. */
static void pass_query_seconds(uint8_t seconds)
{
    if (seconds >= query_cool_left) {
        query_cool_left = 0u;
        return;
    }
    query_cool_left = (uint8_t)(query_cool_left - seconds);
}

bool modem_send(const uint8_t *frame, uint16_t len, uint16_t expires_in)
{
    uint16_t i;

    if (frame == 0 || len > FRAME_CAP) {
        return false;
    }
    if (tx_count < TX_KEEP) {
        for (i = 0u; i < len; ++i) {
            tx_frame[tx_count][i] = frame[i];
        }
        tx_len_at[tx_count] = len;
        tx_expire_at[tx_count] = (uint16_t)(now + expires_in);
    }
    ++tx_count;
    return true;
}

bool modem_queue_viscous(const uint8_t *frame, uint16_t len, uint16_t expire)
{
    return pktq_put(PKTQ_VISCOUS, frame, len, expire);
}

void modem_rx_release(void)
{
}

void telemetry_note_viscous(void)
{
    ++viscous_note_count;
}

void note_drop_f(void)
{
    ++format_drops;
}

void logging_frame(char kind, const uint8_t *frame, uint16_t len)
{
    (void)frame;
    (void)len;
    if (kind == 'V' && g_config.logging != 0u) {
        ++viscous_log_count;
    }
}

static void blank(uint8_t *call)
{
    uint8_t i;

    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        call[i] = (uint8_t)' ';
    }
}

static void put_text(uint8_t *call, const char *text)
{
    uint8_t i;

    blank(call);
    for (i = 0u; text[i] != '\0' && i < CALLSIGN_LEN; ++i) {
        call[i] = (uint8_t)text[i];
    }
}

static void setup(void)
{
    uint8_t a;

    memset(&g_config, 0, sizeof g_config);
    put_text(g_config.mycall, "WFDIGI");
    g_config.mycall_ssid = 0u;
    g_config.digipeat = 1u;
    g_config.directonly = 0u;
    g_config.logging = 1u;
    g_config.maxhops = 3u;
    for (a = 0u; a < ALIAS_COUNT; ++a) {
        blank(g_config.alias[a]);
    }
    for (a = 0u; a < NNALIAS_COUNT; ++a) {
        blank(g_config.nnalias[a]);
    }
    for (a = 0u; a < BPATH_SLOTS; ++a) {
        blank(g_config.bpath[a]);
    }
    for (a = 0u; a < DROP_COUNT; ++a) {
        blank(g_config.droplist[a]);
        g_config.droplist_ssid[a] = 0u;
    }
    now = 1000u;
    g_config.txstale = TX_STALE_DEFAULT;
    dupe_init();
    heard_init();
    messaging_init();
    query_cool_left = 0u;
    beacon_now_count = 0;
    tel_def_count = 0;
    pktq_init();
    tx_count = 0;
    viscous_log_count = 0;
    viscous_note_count = 0;
    format_drops = 0;
    random_count = 0u;
    random_at = 0u;
}

static void set_alias(uint8_t slot, const char *text, uint8_t ssid)
{
    put_text(g_config.alias[slot], text);
    g_config.alias_ssid[slot] = ssid;
}

static void set_nn(uint8_t slot, const char *text)
{
    put_text(g_config.nnalias[slot], text);
}

static void set_drop(uint8_t slot, const char *text, uint8_t ssid)
{
    put_text(g_config.droplist[slot], text);
    g_config.droplist_ssid[slot] = ssid;
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static int unescape(const char *s, uint8_t *dest, uint16_t *len)
{
    uint16_t n = 0u;

    while (*s != '\0') {
        unsigned char c = (unsigned char)*s++;

        if (c == '\\') {
            if (*s == 'r') {
                c = '\r';
                ++s;
            } else if (*s == 'n') {
                c = '\n';
                ++s;
            } else if (*s == '\\') {
                c = '\\';
                ++s;
            } else if (*s == 'x') {
                int hi = hex_nibble(s[1]);
                int lo = hex_nibble(s[2]);

                if (hi < 0 || lo < 0) {
                    return -1;
                }
                c = (unsigned char)((hi << 4) | lo);
                s += 3;
            } else {
                return -1;
            }
        }
        if (n >= FRAME_CAP) {
            return -1;
        }
        dest[n] = c;
        ++n;
    }
    *len = n;
    return 0;
}

struct Addr {
    char call[CALLSIGN_LEN + 1u];
    uint8_t ssid;
    int repeated;
};

static int parse_addr(const char **ps, struct Addr *addr, int allow_star)
{
    const char *s = *ps;
    int n = 0;

    memset(addr, 0, sizeof *addr);
    while (n < (int)CALLSIGN_LEN) {
        char c = *s;

        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')) {
            addr->call[n] = c;
        } else {
            break;
        }
        ++s;
        ++n;
    }
    if (n == 0) {
        return -1;
    }
    if (*s == '-') {
        int value = 0;
        int digits = 0;

        ++s;
        while (*s >= '0' && *s <= '9') {
            value = value * 10 + (*s - '0');
            ++s;
            ++digits;
            if (digits > 2) {
                return -1;
            }
        }
        if (digits == 0 || value > 15) {
            return -1;
        }
        addr->ssid = (uint8_t)value;
    }
    if (*s == '*') {
        if (!allow_star) {
            return -1;
        }
        addr->repeated = 1;
        ++s;
    }
    *ps = s;
    return 0;
}

static void put_addr(uint8_t *raw, const struct Addr *addr, int last)
{
    uint8_t i;

    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        char c = addr->call[i];

        if (c == '\0') {
            c = ' ';
        }
        raw[i] = (uint8_t)((uint8_t)c << 1);
    }
    raw[6] = (uint8_t)(0x60u | (uint8_t)((addr->ssid & 0x0Fu) << 1));
    if (addr->repeated) {
        raw[6] = (uint8_t)(raw[6] | 0x80u);
    }
    if (last) {
        raw[6] = (uint8_t)(raw[6] | 0x01u);
    }
}

static int encode(const char *text, uint8_t *out, uint16_t *out_len)
{
    const char *s = text;
    struct Addr src;
    struct Addr dst;
    struct Addr via[8];
    uint8_t info[FRAME_CAP];
    uint16_t info_len = 0u;
    uint16_t overhead;
    int vias = 0;
    int i;
    uint16_t at;

    if (parse_addr(&s, &src, 0) != 0 || *s != '>') {
        return -1;
    }
    ++s;
    if (parse_addr(&s, &dst, 0) != 0) {
        return -1;
    }
    while (*s == ',') {
        ++s;
        if (vias >= 8) {
            return -1;
        }
        if (parse_addr(&s, &via[vias], 1) != 0) {
            return -1;
        }
        ++vias;
    }
    if (*s != ':') {
        return -1;
    }
    ++s;
    if (unescape(s, info, &info_len) != 0) {
        return -1;
    }
    overhead = (uint16_t)(((2 + vias) * 7) + 2);
    if (info_len > (uint16_t)(FRAME_CAP - overhead)) {
        return -1;
    }
    put_addr(out, &dst, 0);
    put_addr(out + 7, &src, vias == 0);
    for (i = 0; i < vias; ++i) {
        put_addr(out + 14 + (7 * i), &via[i], i == vias - 1);
    }
    at = (uint16_t)((2 + vias) * 7);
    out[at] = AX25_UI;
    out[(uint16_t)(at + 1u)] = AX25_PID;
    for (i = 0; i < (int)info_len; ++i) {
        out[(uint16_t)(at + 2u + (uint16_t)i)] = info[i];
    }
    *out_len = (uint16_t)(overhead + info_len);
    return 0;
}

static int append_char(char *dest, int n, int cap, char c)
{
    if (n + 1 >= cap) {
        return -1;
    }
    dest[n] = c;
    dest[n + 1] = '\0';
    return n + 1;
}

static int append_text(char *dest, int n, int cap, const char *text)
{
    while (*text != '\0') {
        n = append_char(dest, n, cap, *text);
        if (n < 0) {
            return -1;
        }
        ++text;
    }
    return n;
}

static int format_addr(char *dest, int cap, const uint8_t *raw)
{
    uint8_t i;
    uint8_t ssid;
    int n = 0;
    int pad = 0;
    char num[4];

    dest[0] = '\0';
    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        char c;

        if ((raw[i] & 0x01u) != 0u) {
            return -1;
        }
        c = (char)((raw[i] >> 1) & 0x7Fu);
        if (c == ' ') {
            pad = 1;
            continue;
        }
        if (pad || (unsigned char)c < 0x21u || (unsigned char)c > 0x7Eu) {
            return -1;
        }
        n = append_char(dest, n, cap, c);
        if (n < 0) {
            return -1;
        }
    }
    if (n == 0) {
        return -1;
    }
    ssid = (uint8_t)((raw[6] >> 1) & 0x0Fu);
    if (ssid != 0u) {
        num[0] = '-';
        if (ssid >= 10u) {
            num[1] = '1';
            num[2] = (char)('0' + (ssid - 10u));
            num[3] = '\0';
        } else {
            num[1] = (char)('0' + ssid);
            num[2] = '\0';
        }
        n = append_text(dest, n, cap, num);
    }
    if ((raw[6] & 0x80u) != 0u) {
        n = append_char(dest, n, cap, '*');
    }
    return n < 0 ? -1 : 0;
}

static int format_payload(char *dest, int n, int cap, const uint8_t *info, uint16_t len)
{
    uint16_t i;

    for (i = 0u; i < len; ++i) {
        uint8_t c = info[i];
        char hex[5];

        if (c == '\r') {
            n = append_text(dest, n, cap, "\\r");
        } else if (c == '\n') {
            n = append_text(dest, n, cap, "\\n");
        } else if (c == '\\') {
            n = append_text(dest, n, cap, "\\\\");
        } else if (c >= 0x20u && c <= 0x7Eu) {
            n = append_char(dest, n, cap, (char)c);
        } else {
            hex[0] = '\\';
            hex[1] = 'x';
            hex[2] = "0123456789abcdef"[c >> 4];
            hex[3] = "0123456789abcdef"[c & 0x0Fu];
            hex[4] = '\0';
            n = append_text(dest, n, cap, hex);
        }
        if (n < 0) {
            return -1;
        }
    }
    return n;
}

/* Address block is destination, source, vias. Text form is source>destination,vias. */
static int decode(const uint8_t *frame, uint16_t len, char *text, int cap)
{
    char addr[10][16];
    uint16_t at = 0u;
    int count = 0;
    int n = 0;
    int i;

    text[0] = '\0';
    while (count < 10 && (uint16_t)(at + 7u) <= len) {
        uint8_t ssid_byte = frame[(uint16_t)(at + 6u)];
        int last = (ssid_byte & 0x01u) != 0u;

        if ((ssid_byte & 0x60u) != 0x60u) {
            return -1;
        }
        if (format_addr(addr[count], (int)sizeof addr[count], frame + at) != 0) {
            return -1;
        }
        at = (uint16_t)(at + 7u);
        ++count;
        if (last) {
            break;
        }
    }
    if (count < 2 || (frame[(uint16_t)(at - 1u)] & 0x01u) == 0u) {
        return -1;
    }
    if ((uint16_t)(at + 2u) > len || frame[at] != AX25_UI || frame[(uint16_t)(at + 1u)] != AX25_PID) {
        return -1;
    }
    n = append_text(text, n, cap, addr[1]);
    n = append_char(text, n, cap, '>');
    n = append_text(text, n, cap, addr[0]);
    for (i = 2; i < count; ++i) {
        n = append_char(text, n, cap, ',');
        n = append_text(text, n, cap, addr[i]);
    }
    n = append_char(text, n, cap, ':');
    if (n < 0) {
        return -1;
    }
    return format_payload(text, n, cap, frame + at + 2, (uint16_t)(len - (at + 2u))) < 0 ? -1 : 0;
}

static void fail(const char *name, const char *detail)
{
    printf("FAIL %s\n  %s\n", name, detail);
    ++failed;
}

static void judge(const char *name, const char *out)
{
    char got[TEXT_CAP];
    char detail[TEXT_CAP + 80];

    if (out == 0) {
        if (tx_count != 0) {
            got[0] = '\0';
            if (tx_count == 1 && decode(tx_frame[0], tx_len_at[0], got, TEXT_CAP) == 0) {
                snprintf(detail, sizeof detail, "transmitted %s", got);
            } else {
                snprintf(detail, sizeof detail, "transmitted %d frame(s)", tx_count);
            }
            fail(name, detail);
            return;
        }
        ++passed;
        return;
    }
    if (tx_count != 1) {
        snprintf(detail, sizeof detail, "transmitted %d frame(s), want 1", tx_count);
        fail(name, detail);
        return;
    }
    if (decode(tx_frame[0], tx_len_at[0], got, TEXT_CAP) != 0) {
        fail(name, "transmitted frame is not the AX.25 UI shape under test");
        return;
    }
    if (strcmp(got, out) != 0) {
        snprintf(detail, sizeof detail, "got %s", got);
        printf("FAIL %s\n  want %s\n  %s\n", name, out, detail);
        ++failed;
        return;
    }
    ++passed;
}

static void expect_dupe_count(const char *name, uint16_t want)
{
    uint16_t got = dupe_count();
    char detail[80];

    if (got != want) {
        snprintf(detail, sizeof detail, "dupe count %u, want %u", (unsigned)got, (unsigned)want);
        fail(name, detail);
        return;
    }
    ++passed;
}

static void expect_beacon(const char *name, int want)
{
    char detail[80];

    if (beacon_now_count != want) {
        snprintf(detail, sizeof detail, "beacon calls %d, want %d", beacon_now_count, want);
        fail(name, detail);
        return;
    }
    ++passed;
}

static void expect_tel_defs(const char *name, int want)
{
    char detail[80];

    if (tel_def_count != want) {
        snprintf(detail, sizeof detail, "definition sends %d, want %d", tel_def_count, want);
        fail(name, detail);
        return;
    }
    ++passed;
}

static void check_keep(const char *name, const char *in, const char *out)
{
    uint8_t frame[FRAME_CAP];
    uint16_t len = 0u;

    tx_count = 0;
    if (encode(in, frame, &len) != 0) {
        fail(name, "could not build the test frame");
        return;
    }
    digi_ingress(frame, len);
    judge(name, out);
}

static void check_two(const char *name, const char *in, const char *first, const char *second)
{
    uint8_t frame[FRAME_CAP];
    uint16_t len = 0u;
    char got[TEXT_CAP];
    char detail[TEXT_CAP + 80];

    tx_count = 0;
    if (encode(in, frame, &len) != 0) {
        fail(name, "could not build the test frame");
        return;
    }
    digi_ingress(frame, len);
    if (tx_count != 2) {
        snprintf(detail, sizeof detail, "transmitted %d frame(s), want 2", tx_count);
        fail(name, detail);
        return;
    }
    if (decode(tx_frame[0], tx_len_at[0], got, TEXT_CAP) != 0 || strcmp(got, first) != 0) {
        snprintf(detail, sizeof detail, "first got %s", got);
        fail(name, detail);
        return;
    }
    if (decode(tx_frame[1], tx_len_at[1], got, TEXT_CAP) != 0 || strcmp(got, second) != 0) {
        snprintf(detail, sizeof detail, "second got %s", got);
        fail(name, detail);
        return;
    }
    ++passed;
}

static void check_frames(const char *name, const char *in, const char **out, int count)
{
    uint8_t frame[FRAME_CAP];
    uint16_t len = 0u;
    char got[TEXT_CAP];
    char detail[TEXT_CAP + 80];
    int i;

    tx_count = 0;
    if (count > TX_KEEP) {
        fail(name, "the test keeps too few transmitted frames");
        return;
    }
    if (encode(in, frame, &len) != 0) {
        fail(name, "could not build the test frame");
        return;
    }
    digi_ingress(frame, len);
    if (tx_count != count) {
        snprintf(detail, sizeof detail, "transmitted %d frame(s), want %d", tx_count, count);
        fail(name, detail);
        return;
    }
    for (i = 0; i < count; ++i) {
        got[0] = '\0';
        if (decode(tx_frame[i], tx_len_at[i], got, TEXT_CAP) != 0 || strcmp(got, out[i]) != 0) {
            snprintf(detail, sizeof detail, "frame %d got %s", i + 1, got);
            fail(name, detail);
            return;
        }
    }
    ++passed;
}

static void check_len(const char *name, const char *in, uint16_t want_len, const char *out)
{
    uint8_t frame[FRAME_CAP];
    uint16_t len = 0u;
    char detail[80];

    dupe_init();
    tx_count = 0;
    if (encode(in, frame, &len) != 0) {
        fail(name, "could not build the test frame");
        return;
    }
    if (len != want_len) {
        snprintf(detail, sizeof detail, "test frame is %u bytes, want %u", (unsigned)len, (unsigned)want_len);
        fail(name, detail);
        return;
    }
    digi_ingress(frame, len);
    judge(name, out);
}

static void check(const char *name, const char *in, const char *out)
{
    dupe_init();
    check_keep(name, in, out);
}

struct Case {
    const char *name;
    const char *in;
    const char *out;
};

static void run(const struct Case *cases, int count)
{
    int i;

    for (i = 0; i < count; ++i) {
        check(cases[i].name, cases[i].in, cases[i].out);
    }
}

static void check_raw_short(const uint8_t *frame);

static void check_roundtrip(const char *text)
{
    uint8_t frame[FRAME_CAP];
    uint16_t len = 0u;
    char got[TEXT_CAP];

    got[0] = '\0';
    if (encode(text, frame, &len) != 0 || decode(frame, len, got, TEXT_CAP) != 0 || strcmp(got, text) != 0) {
        printf("FAIL roundtrip\n  want %s\n  got %s\n", text, got);
        ++failed;
        return;
    }
    ++passed;
}

static void test_roundtrip(void)
{
    check_roundtrip("N0SRC>APRS:Hi");
    check_roundtrip("N0SRC-12>APRS,AAA*,WIDE2-1*:Hi");
    check_roundtrip("N0SRC>APRS,WFDIGI-15*:Hello\\r");
    check_roundtrip("N0SRC>APRS,WIDE1-1:");
}

static void test_gates(void)
{
    static const uint8_t short_frame[10] = {0};

    setup();
    check("no path", "N0SRC>APRS:Hi", 0);
    check("destination is not a via", "N0SRC>WFDIGI:Hi", 0);
    check("callsign prefix is not MYCALL", "N0SRC>APRS,WFDIG:Hi", 0);
    check_raw_short(short_frame);
    set_nn(0, "WIDE");
    check("own source", "WFDIGI>APRS,WIDE1-1:Hi", 0);
    check("own source with SSID", "WFDIGI-1>APRS,WIDE1-1:Hi", "WFDIGI-1>APRS,WFDIGI*:Hi");
    g_config.digipeat = 0u;
    check("digipeat off", "N0SRC>APRS,WIDE1-1:Hi", 0);
    check("digipeat off ignores MYCALL", "N0SRC>APRS,WFDIGI:Hi", 0);
}

static void check_raw_short(const uint8_t *frame)
{
    dupe_init();
    tx_count = 0;
    digi_ingress(frame, 10u);
    judge("truncated frame", 0);
}

/* Destination, source, and nine digipeaters. The eighth via is not the end. */
static void test_ninth_via(void)
{
    uint8_t frame[(11u * 7u) + 3u];
    uint16_t len = 0u;
    uint8_t i;

    setup();
    set_nn(0, "WIDE");
    for (i = 0u; i < 11u; ++i) {
        uint8_t a;

        for (a = 0u; a < 6u; ++a) {
            frame[len] = (uint8_t)((uint8_t)'A' << 1);
            ++len;
        }
        frame[len] = 0x60u;
        if (i == 10u) {
            frame[len] = (uint8_t)(frame[len] | 0x01u);
        }
        ++len;
    }
    frame[len] = AX25_UI;
    ++len;
    frame[len] = AX25_PID;
    ++len;
    frame[len] = (uint8_t)'H';
    ++len;
    digi_ingress(frame, len);
    judge("ninth via is not repeated", 0);
    if (format_drops != 1) {
        fail("ninth via is not repeated", "missing !F");
    } else {
        ++passed;
    }
}

static void test_mycall(void)
{
    static const struct Case cases[] = {
        {"MYCALL alone", "N0SRC>APRS,WFDIGI:Hi", "N0SRC>APRS,WFDIGI*:Hi"},
        {"preempt earlier hops", "N0SRC>APRS,AAA,WFDIGI:Hi", "N0SRC>APRS,AAA*,WFDIGI*:Hi"},
        {"preempt already used hops", "N0SRC>APRS,AAA*,BBB,WFDIGI:Hi", "N0SRC>APRS,AAA*,BBB*,WFDIGI*:Hi"},
        {"leave hops after MYCALL", "N0SRC>APRS,AAA,WFDIGI,BBB:Hi", "N0SRC>APRS,AAA*,WFDIGI*,BBB:Hi"},
        {"used MYCALL is finished", "N0SRC>APRS,WFDIGI*:Hi", 0},
        {"used MYCALL is a loop", "N0SRC>APRS,AAA,WFDIGI*,BBB,WFDIGI:Hi", 0},
        {"long preempt is not quashed", "N0SRC>APRS,AAA,BBB,CCC,DDD,WFDIGI:Hi",
         "N0SRC>APRS,AAA*,BBB*,CCC*,DDD*,WFDIGI*:Hi"},
        {"hops after a long preempt stay", "N0SRC>APRS,AAA,WFDIGI,BBB,CCC,DDD:Hi",
         "N0SRC>APRS,AAA*,WFDIGI*,BBB,CCC,DDD:Hi"},
    };

    setup();
    run(cases, (int)(sizeof cases / sizeof cases[0]));

    g_config.maxhops = 3u;
    check("used hops block MYCALL", "N0SRC>APRS,AAA*,BBB*,CCC*,WFDIGI:Hi", 0);
    check("one under the used-hop limit", "N0SRC>APRS,AAA*,BBB*,WFDIGI:Hi", "N0SRC>APRS,AAA*,BBB*,WFDIGI*:Hi");

    g_config.directonly = 1u;
    check("DIRECTONLY still repeats MYCALL", "N0SRC>APRS,AAA*,WFDIGI:Hi", "N0SRC>APRS,AAA*,WFDIGI*:Hi");
    set_nn(0, "WIDE");
    check("DIRECTONLY MYCALL with a used n-N", "N0SRC>APRS,WIDE2-1,WFDIGI:Hi", "N0SRC>APRS,WIDE2-1*,WFDIGI*:Hi");
    check("used MYCALL is a loop before an n-N", "N0SRC>APRS,WFDIGI*,WIDE2-2:Hi", 0);

    setup();
    g_config.mycall_ssid = 3u;
    check("MYCALL SSID", "N0SRC>APRS,WFDIGI-3:Hi", "N0SRC>APRS,WFDIGI-3*:Hi");
    check("different SSID is not MYCALL", "N0SRC>APRS,WFDIGI:Hi", 0);
    check("other SSID is not MYCALL", "N0SRC>APRS,WFDIGI-4:Hi", 0);
}

static void test_alias(void)
{
    static const struct Case cases[] = {
        {"alias replaces the via", "N0SRC>APRS,TEMP:Hi", "N0SRC>APRS,WFDIGI*:Hi"},
        {"alias marks earlier hops", "N0SRC>APRS,AAA,TEMP:Hi", "N0SRC>APRS,AAA*,WFDIGI*:Hi"},
        {"alias keeps a used hop", "N0SRC>APRS,AAA*,TEMP:Hi", "N0SRC>APRS,AAA*,WFDIGI*:Hi"},
        {"hops after an alias stay", "N0SRC>APRS,TEMP,BBB:Hi", "N0SRC>APRS,WFDIGI*,BBB:Hi"},
        {"used alias is skipped", "N0SRC>APRS,TEMP*,RELAY-1:Hi", "N0SRC>APRS,TEMP*,WFDIGI*:Hi"},
        {"first unused alias in the path", "N0SRC>APRS,RELAY-1,TEMP:Hi", "N0SRC>APRS,WFDIGI*,TEMP:Hi"},
        {"alias SSID must match", "N0SRC>APRS,RELAY:Hi", 0},
        {"other alias SSID is not used", "N0SRC>APRS,RELAY-2:Hi", 0},
        {"fourth alias slot", "N0SRC>APRS,AL3:Hi", "N0SRC>APRS,WFDIGI*:Hi"},
        {"MYCALL wins over an earlier alias", "N0SRC>APRS,TEMP,WFDIGI:Hi", "N0SRC>APRS,TEMP*,WFDIGI*:Hi"},
        {"used MYCALL blocks a later alias", "N0SRC>APRS,WFDIGI*,TEMP:Hi", 0},
        {"destination stays when the alias matches", "N0SRC>WFDIGI,TEMP:Hi", "N0SRC>WFDIGI,WFDIGI*:Hi"},
    };

    setup();
    set_alias(0, "TEMP", 0u);
    set_alias(1, "RELAY", 1u);
    set_alias(3, "AL3", 0u);
    run(cases, (int)(sizeof cases / sizeof cases[0]));
}

static void test_nnalias(void)
{
    static const struct Case cases[] = {
        {"WIDE2-2 decrements", "N0SRC>APRS,WIDE2-2:Hi", "N0SRC>APRS,WFDIGI*,WIDE2-1:Hi"},
        {"WIDE2-1 is consumed", "N0SRC>APRS,WIDE2-1:Hi", "N0SRC>APRS,WFDIGI*:Hi"},
        {"WIDE1-1 is consumed", "N0SRC>APRS,WIDE1-1:Hi", "N0SRC>APRS,WFDIGI*:Hi"},
        {"WIDE3-3 decrements", "N0SRC>APRS,WIDE3-3:Hi", "N0SRC>APRS,WFDIGI*,WIDE3-2:Hi"},
        {"WIDE3-1 is consumed", "N0SRC>APRS,WIDE3-1:Hi", "N0SRC>APRS,WFDIGI*:Hi"},
        {"WIDE7-7 asks for more than MAXHOPS", "N0SRC>APRS,WIDE7-7:Hi", "N0SRC>APRS,WIDE7-6*,WFDIGI*:Hi"},
        {"WIDE7-1 is consumed", "N0SRC>APRS,WIDE7-1:Hi", "N0SRC>APRS,WFDIGI*:Hi"},
        {"SSID above N is quashed", "N0SRC>APRS,WIDE2-3:Hi", "N0SRC>APRS,WIDE2-3*,WFDIGI*:Hi"},
        {"WIDE1-2 is quashed without a hop", "N0SRC>APRS,WIDE1-2:Hi", "N0SRC>APRS,WIDE1-2*,WFDIGI*:Hi"},
        {"WIDE1-2 quashes a later n-N", "N0SRC>APRS,WIDE1-2,WIDE2-2:Hi",
         "N0SRC>APRS,WIDE1-2*,WIDE2-2*,WFDIGI*:Hi"},
        {"WIDE1-2 does not take a hop from a greedy path", "N0SRC>APRS,WIDE1-2,WIDE5-5:Hi",
         "N0SRC>APRS,WIDE1-2*,WIDE5-5*,WFDIGI*:Hi"},
        {"WIDE1-2 quashes the hops before it", "N0SRC>APRS,AAA,WIDE1-2:Hi",
         "N0SRC>APRS,AAA*,WIDE1-2*,WFDIGI*:Hi"},
        {"used SSID above N is skipped", "N0SRC>APRS,WIDE1-2*,WIDE2-1:Hi", "N0SRC>APRS,WIDE1-2*,WFDIGI*:Hi"},
        {"unconfigured SSID above N is not quashed", "N0SRC>APRS,GATE1-2:Hi", 0},
        {"bare prefix does not match", "N0SRC>APRS,WIDE:Hi", 0},
        {"missing SSID does not match", "N0SRC>APRS,WIDE2:Hi", 0},
        {"We dont get stuck on invalid aliases", "N0SRC>APRS,WIDE2,WIDE2-2:Hi", "N0SRC>APRS,WIDE2*,WFDIGI*,WIDE2-1:Hi"},
        {"SSID 0 does not match", "N0SRC>APRS,WIDE2-0:Hi", 0},
        {"digit above 7 does not match", "N0SRC>APRS,WIDE8-1:Hi", 0},
        {"unconfigured prefix does not match", "N0SRC>APRS,GATE2-2:Hi", 0},
        {"following hop stays and n-N moves to the end", "N0SRC>APRS,WIDE2-2,BBB:Hi",
         "N0SRC>APRS,WFDIGI*,BBB,WIDE2-1:Hi"},
        {"consumed n-N leaves the following hop", "N0SRC>APRS,AAA,WIDE2-1,BBB:Hi", "N0SRC>APRS,AAA*,WFDIGI*,BBB:Hi"},
        {"used n-N is skipped", "N0SRC>APRS,WIDE2-2*,WIDE1-1:Hi", "N0SRC>APRS,WIDE2-2*,WFDIGI*:Hi"},
        {"second WIDE2-2 still decrements", "N0SRC>APRS,WIDE2-2*,WIDE2-2:Hi",
         "N0SRC>APRS,WIDE2-2*,WFDIGI*,WIDE2-1:Hi"},
        {"short prefix does not steal WIDE", "N0SRC>APRS,WIDE2-2:Hi", "N0SRC>APRS,WFDIGI*,WIDE2-1:Hi"},
        {"short prefix matches its own call", "N0SRC>APRS,W2-2:Hi", "N0SRC>APRS,WFDIGI*,W2-1:Hi"},
        {"first unused n-N, reduced call goes last", "N0SRC>APRS,TRACE2-2,WIDE1-1:Hi",
         "N0SRC>APRS,WFDIGI*,WIDE1-1,TRACE2-1:Hi"},
        {"fourth n-N slot", "N0SRC>APRS,QST1-1:Hi", "N0SRC>APRS,WFDIGI*:Hi"},
        {"We dont match subsets of an n-N alias", "N0SRC>APRS,WIDE3G-3:Hi", 0},        
        {"full path quash replaces the last via", "N0SRC>APRS,WIDE2-2,A,B,C,D,E,F,G:Hi",
         "N0SRC>APRS,WIDE2-1*,A*,B*,C*,D*,E*,F*,WFDIGI*:Hi"},
        {"full path quash of WIDE1-2 leaves the SSID", "N0SRC>APRS,WIDE1-2,A,B,C,D,E,F,G:Hi",
         "N0SRC>APRS,WIDE1-2*,A*,B*,C*,D*,E*,F*,WFDIGI*:Hi"},
    };

    setup();
    set_nn(0, "W");
    set_nn(1, "WIDE");
    set_nn(2, "TRACE");
    set_nn(3, "QST");
    run(cases, (int)(sizeof cases / sizeof cases[0]));

    g_config.maxhops = 4u;
    check("three vias under a higher limit", "N0SRC>APRS,AAA,WIDE2-2,BBB:Hi",
          "N0SRC>APRS,AAA*,WFDIGI*,BBB,WIDE2-1:Hi");
    g_config.maxhops = 7u;
    check("seven vias consume WIDE2-1 in place", "N0SRC>APRS,A,B,C,D,E,F,WIDE2-1:Hi",
          "N0SRC>APRS,A*,B*,C*,D*,E*,F*,WFDIGI*:Hi");
    check("full path quash at MAXHOPS 7 replaces the last via", "N0SRC>APRS,A,B,C,D,E,F,G,WIDE2-1:Hi",
          "N0SRC>APRS,A*,B*,C*,D*,E*,F*,G*,WFDIGI*:Hi");

    setup();
    set_alias(0, "WIDE2", 2u);
    set_nn(0, "WIDE");
    check("alias wins over n-N on the same address", "N0SRC>APRS,WIDE2-2:Hi", "N0SRC>APRS,WFDIGI*:Hi");
    g_config.maxhops = 1u;
    check("too many requested hops quash before the alias rewrite", "N0SRC>APRS,WIDE2-2:Hi",
          "N0SRC>APRS,WIDE2-2*,WFDIGI*:Hi");
}

static void test_directonly(void)
{
    setup();
    set_alias(0, "TEMP", 0u);
    set_nn(0, "WIDE");
    check("used hop still matches when DIRECTONLY is off", "N0SRC>APRS,AAA*,WIDE2-2:Hi",
          "N0SRC>APRS,AAA*,WFDIGI*,WIDE2-1:Hi");
    check("used alias still matches when DIRECTONLY is off", "N0SRC>APRS,AAA*,TEMP:Hi", "N0SRC>APRS,AAA*,WFDIGI*:Hi");

    g_config.directonly = 1u;
    check("DIRECTONLY blocks a used hop", "N0SRC>APRS,AAA*,WIDE2-2:Hi", 0);
    check("DIRECTONLY blocks a used alias", "N0SRC>APRS,AAA*,TEMP:Hi", 0);
    check("WIDE2-1 is direct when it is the first hop", "N0SRC>APRS,WIDE2-1:Hi", "N0SRC>APRS,WFDIGI*:Hi");
    check("complete n-N is still fresh", "N0SRC>APRS,WIDE2-2:Hi", "N0SRC>APRS,WFDIGI*,WIDE2-1:Hi");
    check("WIDE1-1,WIDE2-1 is direct", "N0SRC>APRS,WIDE1-1,WIDE2-1:Hi", "N0SRC>APRS,WFDIGI*,WIDE2-1:Hi");
    check("a later WIDE2-1 does not make the path used", "N0SRC>APRS,WIDE2-2,WIDE2-1:Hi",
          "N0SRC>APRS,WFDIGI*,WIDE2-1,WIDE2-1:Hi");
    check("unused ordinary hops are still fresh", "N0SRC>APRS,TEMP,AAA:Hi", "N0SRC>APRS,WFDIGI*,AAA:Hi");
    check("complete n-N then an alias", "N0SRC>APRS,WIDE2-2,TEMP:Hi", "N0SRC>APRS,WIDE2-2*,WFDIGI*:Hi");
    check("WIDE2-1 still reaches a later alias", "N0SRC>APRS,WIDE2-1,TEMP:Hi", "N0SRC>APRS,WIDE2-1*,WFDIGI*:Hi");
    check("unconfigured call does not count as a used n-N", "N0SRC>APRS,RELAY2-1,TEMP:Hi",
          "N0SRC>APRS,RELAY2-1*,WFDIGI*:Hi");
    set_nn(1, "RELAY");
    check("RELAY2-1 is direct when it is the first hop", "N0SRC>APRS,RELAY2-1,TEMP:Hi",
          "N0SRC>APRS,RELAY2-1*,WFDIGI*:Hi");
    check("DIRECTONLY does not block MYCALL", "N0SRC>APRS,AAA*,BBB*,WFDIGI:Hi", "N0SRC>APRS,AAA*,BBB*,WFDIGI*:Hi");
}

static void test_maxhops(void)
{
    setup();
    set_alias(0, "TEMP", 0u);
    set_nn(0, "WIDE");
    check("request equal to MAXHOPS decrements", "N0SRC>APRS,WIDE3-3:Hi", "N0SRC>APRS,WFDIGI*,WIDE3-2:Hi");
    check("WIDE5-5 is quashed", "N0SRC>APRS,WIDE5-5:Hi", "N0SRC>APRS,WIDE5-4*,WFDIGI*:Hi");
    check("WIDE4-4 is quashed", "N0SRC>APRS,WIDE4-4:Hi", "N0SRC>APRS,WIDE4-3*,WFDIGI*:Hi");
    check("WIDE3-2 is under the limit", "N0SRC>APRS,WIDE3-2:Hi", "N0SRC>APRS,WFDIGI*,WIDE3-1:Hi");
    check("used hop plus WIDE2-1 stays a normal repeat", "N0SRC>APRS,AAA,BBB*,WIDE2-1:Hi",
          "N0SRC>APRS,AAA*,BBB*,WFDIGI*:Hi");
    check("used hop plus WIDE2-2 is quashed", "N0SRC>APRS,AAA,BBB*,WIDE2-2:Hi",
          "N0SRC>APRS,AAA*,BBB*,WIDE2-1*,WFDIGI*:Hi");
    check("pending hops equal MAXHOPS", "N0SRC>APRS,AAA,WIDE2-2:Hi", "N0SRC>APRS,AAA*,WFDIGI*,WIDE2-1:Hi");
    check("pending hops above MAXHOPS are quashed", "N0SRC>APRS,AAA,BBB,WIDE2-2:Hi",
          "N0SRC>APRS,AAA*,BBB*,WIDE2-1*,WFDIGI*:Hi");
    check("two WIDE2-2 requests are quashed", "N0SRC>APRS,WIDE2-2,WIDE2-2:Hi",
          "N0SRC>APRS,WIDE2-1*,WIDE2-2*,WFDIGI*:Hi");
    check("alias request equal to MAXHOPS", "N0SRC>APRS,AAA,TEMP:Hi", "N0SRC>APRS,AAA*,WFDIGI*:Hi");
    check("SSID above n quashes an alias path", "N0SRC>APRS,TEMP,WIDE2-3:Hi",
          "N0SRC>APRS,TEMP*,WIDE2-3*,WFDIGI*:Hi");
    check("unconfigured n-N shape counts as one hop", "N0SRC>APRS,TEMP,GATE5-5:Hi", "N0SRC>APRS,WFDIGI*,GATE5-5:Hi");
    check("a used n-N address counts as one hop", "N0SRC>APRS,WIDE5-5*,TEMP:Hi", "N0SRC>APRS,WIDE5-5*,WFDIGI*:Hi");
    check("an unmatched path is not quashed", "N0SRC>APRS,AAA,BBB,CCC,DDD:Hi", 0);
    check("an unmatched n-N shape is not quashed", "N0SRC>APRS,GATE5-5:Hi", 0);

    g_config.maxhops = 2u;
    check("MAXHOPS 2 quashes WIDE3-3", "N0SRC>APRS,WIDE3-3:Hi", "N0SRC>APRS,WIDE3-2*,WFDIGI*:Hi");
    check("MAXHOPS 2 quashes three alias hops", "N0SRC>APRS,AAA,BBB,TEMP:Hi", "N0SRC>APRS,AAA*,BBB*,TEMP*,WFDIGI*:Hi");
    check("MAXHOPS 2 still repeats two hops", "N0SRC>APRS,AAA,TEMP:Hi", "N0SRC>APRS,AAA*,WFDIGI*:Hi");
    check("MAXHOPS 2 still repeats an unconfigured two-hop call", "N0SRC>APRS,TEMP,RELAY2-2:Hi",
          "N0SRC>APRS,WFDIGI*,RELAY2-2:Hi");
    check("MAXHOPS 2 decrements a fresh WIDE2-2", "N0SRC>APRS,WIDE2-2:Hi",
          "N0SRC>APRS,WFDIGI*,WIDE2-1:Hi");
    check("MAXHOPS 2 quashes a used hop plus WIDE2-2", "N0SRC>APRS,AAA*,WIDE2-2:Hi",
          "N0SRC>APRS,AAA*,WIDE2-1*,WFDIGI*:Hi");
    check("already used MAXHOPS drops the frame", "N0SRC>APRS,AAA*,BBB*,WIDE1-1:Hi", 0);
    check("used hops win over a quash", "N0SRC>APRS,AAA*,BBB*,CCC*,WIDE5-5:Hi", 0);

    g_config.maxhops = 1u;
    check("MAXHOPS 1 repeats WIDE1-1", "N0SRC>APRS,WIDE1-1:Hi", "N0SRC>APRS,WFDIGI*:Hi");
    check("MAXHOPS 1 quashes WIDE2-2", "N0SRC>APRS,WIDE2-2:Hi", "N0SRC>APRS,WIDE2-1*,WFDIGI*:Hi");
    check("MAXHOPS 1 quashes WIDE1-2 without a hop", "N0SRC>APRS,WIDE1-2:Hi",
          "N0SRC>APRS,WIDE1-2*,WFDIGI*:Hi");

    g_config.maxhops = 7u;
    check("MAXHOPS 7 repeats WIDE7-7", "N0SRC>APRS,WIDE7-7:Hi", "N0SRC>APRS,WFDIGI*,WIDE7-6:Hi");
    check("MAXHOPS 7 still quashes WIDE1-2", "N0SRC>APRS,WIDE1-2:Hi", "N0SRC>APRS,WIDE1-2*,WFDIGI*:Hi");
    check("seven alias hops stay a normal repeat", "N0SRC>APRS,TEMP,A,B,C,D,E,F:Hi",
          "N0SRC>APRS,WFDIGI*,A,B,C,D,E,F:Hi");

    g_config.maxhops = 6u;
    check("full alias path is quashed in eight vias", "N0SRC>APRS,TEMP,A,B,C,D,E,F,G:Hi",
          "N0SRC>APRS,TEMP*,A*,B*,C*,D*,E*,F*,WFDIGI*:Hi");
    check("seven hops are quashed at MAXHOPS 6", "N0SRC>APRS,TEMP,A,B,C,D,E,F:Hi",
          "N0SRC>APRS,TEMP*,A*,B*,C*,D*,E*,F*,WFDIGI*:Hi");

    g_config.maxhops = 8u;
    check("eight hops are repeated normally", "N0SRC>APRS,TEMP,A,B,C,D,E,F,G:Hi",
          "N0SRC>APRS,WFDIGI*,A,B,C,D,E,F,G:Hi");
    check("eight n-N hops are repeated normally", "N0SRC>APRS,WIDE7-7,WIDE1-1:Hi",
          "N0SRC>APRS,WFDIGI*,WIDE1-1,WIDE7-6:Hi");
    check("nine hops are quashed by appending", "N0SRC>APRS,WIDE7-7,WIDE2-2:Hi",
          "N0SRC>APRS,WIDE7-6*,WIDE2-2*,WFDIGI*:Hi");
    check("nine hops in a full path replace the last via", "N0SRC>APRS,A,B,C,D,E,F,G,WIDE2-2:Hi",
          "N0SRC>APRS,A*,B*,C*,D*,E*,F*,G*,WFDIGI*:Hi");
    check("eight used hops are not repeated", "N0SRC>APRS,A*,B*,C*,D*,E*,F*,G*,WIDE1-1*:Hi", 0);

    g_config.maxhops = 3u;
    g_config.directonly = 1u;
    check("a fresh oversized n-N is still quashed", "N0SRC>APRS,WIDE5-5:Hi", "N0SRC>APRS,WIDE5-4*,WFDIGI*:Hi");
    check("DIRECTONLY drops before a quash", "N0SRC>APRS,AAA*,WIDE5-5:Hi", 0);
    check("DIRECTONLY quashes a fresh WIDE1-2", "N0SRC>APRS,WIDE1-2:Hi", "N0SRC>APRS,WIDE1-2*,WFDIGI*:Hi");
    check("DIRECTONLY drops a used hop before WIDE1-2", "N0SRC>APRS,AAA*,WIDE1-2:Hi", 0);
    g_config.directonly = 0u;
    check("two used hops still quash WIDE5-5", "N0SRC>APRS,AAA*,BBB*,WIDE5-5:Hi",
          "N0SRC>APRS,AAA*,BBB*,WIDE5-4*,WFDIGI*:Hi");
}

static void fill_char(char *dest, int n, char c)
{
    int i;

    for (i = 0; i < n; ++i) {
        dest[i] = c;
    }
    dest[n] = '\0';
}

static void test_limits(void)
{
    char in[400];
    char out[400];
    char payload[320];

    setup();
    set_nn(0, "WIDE");
    /* Three addresses and UI/PID are 23 bytes. 301 more is 324, which cannot grow. */
    fill_char(payload, 301, 'B');
    snprintf(in, sizeof in, "N0SRC>APRS,WIDE2-2:%s", payload);
    check_len("no room to append WIDE2-1", in, 324u, 0);
    if (format_drops != 1) {
        fail("no room to append WIDE2-1", "missing !F");
    } else {
        ++passed;
    }
    snprintf(in, sizeof in, "N0SRC>APRS,WIDE2-1:%s", payload);
    snprintf(out, sizeof out, "N0SRC>APRS,WFDIGI*:%s", payload);
    check_len("in-place repeat still fits", in, 324u, out);

    fill_char(payload, 300, 'B');
    snprintf(in, sizeof in, "N0SRC>APRS,WIDE2-2:%s", payload);
    snprintf(out, sizeof out, "N0SRC>APRS,WFDIGI*,WIDE2-1:%s", payload);
    check_len("append fills the last byte", in, 323u, out);

    check("payload bytes stay put", "N0SRC>APRS,WIDE2-2:AB:CD\\r", "N0SRC>APRS,WFDIGI*,WIDE2-1:AB:CD\\r");
}

static void test_dupe_count(void)
{
    setup();
    set_nn(0, "WIDE");
    expect_dupe_count("empty list", 0u);
    check_keep("remember one", "N0SRC>APRS,WIDE1-1:Hi", "N0SRC>APRS,WFDIGI*:Hi");
    expect_dupe_count("one packet", 1u);
    check_keep("duplicate does not add a slot", "N0SRC>APRS,WIDE1-1:Hi", 0);
    expect_dupe_count("still one packet", 1u);
    check_keep("second station", "N0OTH>APRS,WIDE1-1:Hi", "N0OTH>APRS,WFDIGI*:Hi");
    expect_dupe_count("two packets", 2u);
    now = (uint16_t)(now + DUPE_WINDOW);
    expect_dupe_count("aged entries stay until a scan", 2u);
    dupe_init();
    expect_dupe_count("init clears the list", 0u);
}

static void test_dupes(void)
{
    int i;
    char in[64];
    char out[64];

    setup();
    set_nn(0, "WIDE");
    set_alias(0, "TEMP", 0u);
    now = 1000u;
    check_keep("first copy", "N0SRC>APRS,WIDE1-1:Hi", "N0SRC>APRS,WFDIGI*:Hi");
    check_keep("same frame inside the window", "N0SRC>APRS,WIDE1-1:Hi", 0);
    now = 1029u;
    check_keep("29 seconds is still a duplicate", "N0SRC>APRS,WIDE1-1:Hi", 0);
    now = 1030u;
    check_keep("30 seconds lets it repeat", "N0SRC>APRS,WIDE1-1:Hi", "N0SRC>APRS,WFDIGI*:Hi");
    now = 1059u;
    check_keep("a sent copy starts the window again", "N0SRC>APRS,WIDE1-1:Hi", 0);

    dupe_init();
    now = 2000u;
    check_keep("different text", "N0SRC>APRS,WIDE1-1:Hi", "N0SRC>APRS,WFDIGI*:Hi");
    check_keep("other text is not a duplicate", "N0SRC>APRS,WIDE1-1:Ho", "N0SRC>APRS,WFDIGI*:Ho");
    check_keep("other source", "N0OTH>APRS,WIDE1-1:Hi", "N0OTH>APRS,WFDIGI*:Hi");
    check_keep("other source SSID", "N0SRC-2>APRS,WIDE1-1:Hi", "N0SRC-2>APRS,WFDIGI*:Hi");
    check_keep("same source SSID is a duplicate", "N0SRC>APRS,WIDE1-1:Hi", 0);
    check_keep("same text by another path", "N0SRC>APRS,TEMP:Hi", 0);
    check_keep("same text to another destination", "N0SRC>BEACON,WIDE1-1:Hi", 0);

    dupe_init();
    check_keep("information field", "N0SRC>APRS,WIDE1-1:Hello", "N0SRC>APRS,WFDIGI*:Hello");
    check_keep("trailing CR is a different hash", "N0SRC>APRS,WIDE1-1:Hello\\r", "N0SRC>APRS,WFDIGI*:Hello\\r");
    dupe_init();
    check_keep("information field again", "N0SRC>APRS,WIDE1-1:Hello", "N0SRC>APRS,WFDIGI*:Hello");
    check_keep("trailing LF is a different hash", "N0SRC>APRS,WIDE1-1:Hello\\n", "N0SRC>APRS,WFDIGI*:Hello\\n");
    dupe_init();
    check_keep("letters", "N0SRC>APRS,WIDE1-1:Hello", "N0SRC>APRS,WFDIGI*:Hello");
    check_keep("embedded byte is a different hash", "N0SRC>APRS,WIDE1-1:H\\x01ello", "N0SRC>APRS,WFDIGI*:H\\x01ello");
    check_keep("CR between letters", "N0SRC>APRS,WIDE1-1:A\\rB", "N0SRC>APRS,WFDIGI*:A\\rB");
    check_keep("AB is a different hash", "N0SRC>APRS,WIDE1-1:AB", "N0SRC>APRS,WFDIGI*:AB");

    dupe_init();
    check_keep("empty information field", "N0SRC>APRS,WIDE1-1:", "N0SRC>APRS,WFDIGI*:");
    check_keep("printable text differs from empty", "N0SRC>APRS,WIDE1-1:@@@@", "N0SRC>APRS,WFDIGI*:@@@@");
    check_keep("non-printable field differs from empty", "N0SRC>APRS,WIDE1-1:\\x00\\xff", "N0SRC>APRS,WFDIGI*:\\x00\\xff");
    check_keep("a real hash is different", "N0SRC>APRS,WIDE1-1:A", "N0SRC>APRS,WFDIGI*:A");

    dupe_init();
    check_keep("hash keeps AB", "N0SRC>APRS,WIDE1-1:AB", "N0SRC>APRS,WFDIGI*:AB");
    check_keep("BA is not the same hash", "N0SRC>APRS,WIDE1-1:BA", "N0SRC>APRS,WFDIGI*:BA");
    check_keep("same text from another station", "N0OTH>APRS,WIDE1-1:BA", "N0OTH>APRS,WFDIGI*:BA");

    setup();
    set_nn(0, "WIDE");
    g_config.digipeat = 0u;
    now = 3000u;
    check_keep("off does not remember the frame", "N0SRC>APRS,WIDE1-1:Hi", 0);
    g_config.digipeat = 1u;
    check_keep("frame was not remembered", "N0SRC>APRS,WIDE1-1:Hi", "N0SRC>APRS,WFDIGI*:Hi");
    check_keep("the repeat is now remembered", "N0SRC>APRS,WIDE1-1:Hi", 0);

    dupe_init();
    check_keep("own source is not remembered", "WFDIGI>APRS,WIDE1-1:Own", 0);
    check_keep("another station with that text", "N0SRC>APRS,WIDE1-1:Own", "N0SRC>APRS,WFDIGI*:Own");

    now = 65520u;
    dupe_init();
    check_keep("sent near the clock wrap", "N0SRC>APRS,WIDE1-1:Wrap", "N0SRC>APRS,WFDIGI*:Wrap");
    now = 13u;
    check_keep("29 seconds across the wrap", "N0SRC>APRS,WIDE1-1:Wrap", 0);
    now = 14u;
    check_keep("30 seconds across the wrap", "N0SRC>APRS,WIDE1-1:Wrap", "N0SRC>APRS,WFDIGI*:Wrap");

    dupe_init();
    now = 5000u;
    for (i = 0; i < 250; ++i) {
        if (i == 1) {
            now = 5001u;
        }
        snprintf(in, sizeof in, "S%03d>APRS,WIDE1-1:P%03d", i, i);
        snprintf(out, sizeof out, "S%03d>APRS,WFDIGI*:P%03d", i, i);
        check_keep("fill the duplicate list", in, out);
    }
    check_keep("new packet replaces the oldest", "SNEW>APRS,WIDE1-1:NEW", "SNEW>APRS,WFDIGI*:NEW");
    check_keep("replaced entry can repeat", "S000>APRS,WIDE1-1:P000", "S000>APRS,WFDIGI*:P000");
    check_keep("newer entry is still held", "S001>APRS,WIDE1-1:P001", 0);
    now = 5031u;
    check_keep("expired entries are cleared", "S001>APRS,WIDE1-1:P001", "S001>APRS,WFDIGI*:P001");
    check_keep("a cleared new packet can repeat", "SNEW>APRS,WIDE1-1:NEW", "SNEW>APRS,WFDIGI*:NEW");
}

static bool ingress_text(const char *text)
{
    uint8_t frame[FRAME_CAP];
    uint16_t len = 0u;

    if (encode(text, frame, &len) != 0) {
        return false;
    }
    digi_ingress(frame, len);
    return true;
}

static void expect_expire(const char *name, uint16_t want)
{
    char detail[80];

    if (tx_count != 1 || tx_expire_at[0] != want) {
        snprintf(detail, sizeof detail, "expiry %u, want %u",
                 tx_count == 1 ? (unsigned)tx_expire_at[0] : 0u, (unsigned)want);
        fail(name, detail);
        return;
    }
    ++passed;
}

static void test_viscous(void)
{
    setup();
    g_config.directonly = 1u;
    g_config.viscous_min = 3u;
    g_config.viscous_max = 5u;
    if (!ingress_text("N0SRC>APRS,WFDIGI:Specific")) {
        fail("VISCOUS immediate MYCALL", "could not build frame");
        return;
    }
    judge("VISCOUS does not delay MYCALL", "N0SRC>APRS,WFDIGI*:Specific");
    if (pktq_pending(PKTQ_VISCOUS)) {
        fail("VISCOUS leaves MYCALL queue empty", "frame entered viscous queue");
    } else {
        ++passed;
    }

    setup();
    g_config.directonly = 1u;
    g_config.viscous_min = 3u;
    g_config.viscous_max = 5u;
    if (!ingress_text("N0SRC>APRS,OTHER*,WFDIGI:Specific")) {
        fail("VISCOUS non-direct MYCALL", "could not build frame");
        return;
    }
    judge("VISCOUS does not suppress non-direct MYCALL",
          "N0SRC>APRS,OTHER*,WFDIGI*:Specific");

    setup();
    set_nn(0, "WIDE");
    g_config.directonly = 1u;
    g_config.viscous_min = 3u;
    g_config.viscous_max = 5u;
    random_draw[0] = 0u;
    random_count = 1u;
    if (!ingress_text("N0SRC>APRS,WIDE2-2:Delay")) {
        fail("VISCOUS queues direct packet", "could not build frame");
        return;
    }
    judge("VISCOUS waits before transmit", 0);
    now = 1002u;
    digi_service();
    judge("VISCOUS still waits before expiry", 0);
    now = 1003u;
    digi_service();
    judge("VISCOUS promotes at expiry", "N0SRC>APRS,WFDIGI*,WIDE2-1:Delay");
    expect_expire("VISCOUS preserves stale deadline", 1010u);

    setup();
    set_nn(0, "WIDE");
    g_config.directonly = 1u;
    g_config.viscous_min = 3u;
    g_config.viscous_max = 3u;
    if (!ingress_text("N0SRC>APRS,WIDE2-2:Suppressed")) {
        fail("VISCOUS suppression original", "could not build frame");
        return;
    }
    now = 1001u;
    if (!ingress_text("N0SRC>APRS,OTHER*,WIDE2-1:Suppressed")) {
        fail("VISCOUS suppression copy", "could not build frame");
        return;
    }
    now = 1003u;
    digi_service();
    judge("VISCOUS suppresses a repeated copy", 0);
    if (viscous_log_count != 1) {
        fail("VISCOUS logs a suppressed repeat", "missing V trace");
    } else {
        ++passed;
    }
    if (viscous_note_count != 1) {
        fail("VISCOUS counts a suppressed repeat", "missing telemetry count");
    } else {
        ++passed;
    }

    setup();
    set_nn(0, "WIDE");
    g_config.directonly = 1u;
    g_config.viscous_min = 1u;
    g_config.viscous_max = 9u;
    random_draw[0] = 8u;
    random_draw[1] = 0u;
    random_count = 2u;
    if (!ingress_text("N0ONE>APRS,WIDE1-1:Late") ||
        !ingress_text("N0TWO>APRS,WIDE1-1:Early")) {
        fail("VISCOUS expiry ordering", "could not build frame");
        return;
    }
    now = 1001u;
    digi_service();
    judge("VISCOUS earliest expiry goes first", "N0TWO>APRS,WFDIGI*:Early");
    tx_count = 0;
    now = 1009u;
    digi_service();
    judge("VISCOUS later expiry follows", "N0ONE>APRS,WFDIGI*:Late");
}

static void test_messaging(void)
{
    setup();
    check_keep("direct general query is not digipeated without a path", "N0SRC>APRS:?APRS?", 0);
    expect_beacon("direct general query beacons", 1);
    expect_dupe_count("general query is not recorded", 0u);

    setup();
    set_nn(0, "WIDE");
    check_keep("general query is digipeated", "N0SRC>APRS,WIDE1-1:?APRS?", "N0SRC>APRS,WFDIGI*:?APRS?");
    expect_beacon("general query beacons", 1);
    pass_query_seconds(30u);
    check_keep("general query with an argument is digipeated", "N0SRC>APRS,WIDE1-1:?APRS? extra",
               "N0SRC>APRS,WFDIGI*:?APRS? extra");
    expect_beacon("general query with an argument beacons", 2);
    pass_query_seconds(29u);
    check_keep("another station during cooldown", "N0OTH>APRS,WIDE1-1:?APRS?", "N0OTH>APRS,WFDIGI*:?APRS?");
    expect_beacon("cooldown holds the beacon", 2);
    pass_query_seconds(1u);
    check_keep("cooldown expires at 30 seconds", "N0NEW>APRS,WIDE1-1:?APRS?", "N0NEW>APRS,WFDIGI*:?APRS?");
    expect_beacon("cooldown allows another beacon", 3);

    setup();
    check_keep("numbered message", "N0SRC>APRS::WFDIGI   :Hello{12", "WFDIGI>APWFDP*::N0SRC    :ack12");
    expect_dupe_count("ack is recorded", 1u);
    expect_beacon("ordinary message does not beacon", 0);
    check_keep("echo is not answered again", "N0SRC>APRS::WFDIGI   :Hello{12", 0);
    expect_dupe_count("echo adds no duplicate entry", 1u);

    setup();
    set_nn(0, "WIDE");
    check_keep("directed message is not digipeated", "N0SRC>APRS,WIDE1-1::WFDIGI   :Hello", 0);
    check_keep("message for someone else is digipeated", "N0SRC>APRS,WIDE1-1::N0OTH    :Hello{1",
               "N0SRC>APRS,WFDIGI*::N0OTH    :Hello{1");

    setup();
    g_config.bpath_count = 1u;
    put_text(g_config.bpath[0], "WIDE1");
    g_config.bpath_ssid[0] = 1u;
    check_keep("ack uses the first beacon path", "N0SRC>APRS::WFDIGI   :Hello{4",
               "WFDIGI>APWFDP*,WIDE1-1::N0SRC    :ack4");

    setup();
    g_config.mycall_ssid = 3u;
    check_keep("ssid is part of the addressee", "N0SRC-2>APRS::WFDIGI-3 :Ping{7",
               "WFDIGI-3>APWFDP*::N0SRC-2  :ack7");
    setup();
    set_nn(0, "WIDE");
    check_keep("ssid 0 omits the dash", "N0SRC>APRS,WIDE1-1::WFDIGI-0 :Hi{1",
               "N0SRC>APRS,WFDIGI*::WFDIGI-0 :Hi{1");

    setup();
    check_keep("lowercase addressee matches", "N0SRC>APRS::wfdigi   :Hello{1", "WFDIGI>APWFDP*::N0SRC    :ack1");

    setup();
    check_keep("directed position query", "N0SRC>APRS::WFDIGI   :?APRSP", 0);
    expect_beacon("directed position query beacons", 1);
    pass_query_seconds(30u);
    check_keep("position query with an argument", "N0OTH>APRS::WFDIGI   :?APRSP please", 0);
    expect_beacon("position query with an argument beacons", 2);
    check_keep("bare APRS is not a query", "N0SRC>APRS::WFDIGI   :?APRS", 0);
    check_keep("spaced APRS is not a query", "N0SRC>APRS::WFDIGI   :?APRS ", 0);
    check_keep("longer token is not a query", "N0SRC>APRS::WFDIGI   :?APRSPX", 0);
    expect_beacon("only a single query letter beacons", 2);

    setup();
    check_keep("directed supported query list", "N0SRC>APRS::WFDIGI   :?APRS?",
               "WFDIGI>APWFDP*::N0SRC    :?APRSD ?APRSP ?APRSU ?APRSV");
    expect_beacon("directed supported query list does not beacon", 0);

    setup();
    check_keep("version query", "N0SRC>APRS::WFDIGI   :?APRSV",
               "WFDIGI>APWFDP*::N0SRC    :v9.9-test pk88");
    expect_beacon("version query does not beacon", 0);
    check_keep("version query echo", "N0SRC>APRS::WFDIGI   :?APRSV", 0);

    setup();
    check_keep("numbered position query", "N0SRC>APRS::WFDIGI   :?APRSP{9", "WFDIGI>APWFDP*::N0SRC    :ack9");
    expect_beacon("numbered position query beacons after the ack", 1);
    check_keep("echo of a numbered position query", "N0SRC>APRS::WFDIGI   :?APRSP{9", 0);
    expect_beacon("echo does not beacon again", 1);

    setup();
    check_keep("telemetry definition query", "N0SRC>APRS::WFDIGI   :?APRSU", 0);
    expect_tel_defs("telemetry definition query sends the definitions", 1);
    expect_beacon("telemetry definition query does not beacon", 0);
    check_keep("telemetry definition query with an argument", "N0SRC>APRS::WFDIGI   :?APRSU now", 0);
    expect_tel_defs("telemetry definition argument sends the definitions", 2);
    setup();
    check_keep("numbered telemetry definition query", "N0SRC>APRS::WFDIGI   :?APRSU{5",
               "WFDIGI>APWFDP*::N0SRC    :ack5");
    expect_tel_defs("numbered telemetry definition query sends the definitions", 1);

    setup();
    check_keep("unsupported query", "N0SRC>APRS::WFDIGI   :?APRSS",
               "WFDIGI>APWFDP*::N0SRC    :ERR - query type unsupported");
    expect_beacon("unsupported query does not beacon", 0);
    check_keep("unsupported query echo", "N0SRC>APRS::WFDIGI   :?APRSS", 0);
    check_keep("unsupported query with an argument", "N0OTH>APRS::WFDIGI   :?APRSH N0ZZZ",
               "WFDIGI>APWFDP*::N0OTH    :ERR - query type unsupported");
    setup();
    check_two("numbered unsupported query", "N0SRC>APRS::WFDIGI   :?APRSM{3", "WFDIGI>APWFDP*::N0SRC    :ack3",
              "WFDIGI>APWFDP*::N0SRC    :ERR - query type unsupported");
    check_keep("numbered unsupported echo", "N0SRC>APRS::WFDIGI   :?APRSM{3", 0);

    setup();
    set_nn(0, "WIDE");
    check_keep("third-party message", "IGATE>APRS,WIDE1-1:}N0SRC-2>APRS::WFDIGI   :Hello{8",
               "WFDIGI>APWFDP*::N0SRC-2  :ack8");
    check_keep("third-party message is not digipeated", "IGATE>APRS,WIDE1-1:}N0SRC-2>APRS::WFDIGI   :Hello{8", 0);
    check_keep("third-party for someone else is digipeated", "IGATE>APRS,WIDE1-1:}N0SRC>APRS::N0OTH    :Hello{1",
               "IGATE>APRS,WFDIGI*:}N0SRC>APRS::N0OTH    :Hello{1");
    check_keep("malformed third-party is digipeated", "N0SRC>APRS,WIDE1-1:}not-a-header",
               "N0SRC>APRS,WFDIGI*:}not-a-header");
    check_keep("third-party general query is digipeated", "N0SRC>APRS,WIDE1-1:}N0OTH>BEACON:?APRS?",
               "N0SRC>APRS,WFDIGI*:}N0OTH>BEACON:?APRS?");
    expect_beacon("third-party general query beacons", 1);
}

static void expect_heard(const char *name, const char *want)
{
    uint8_t text[TEXT_CAP];
    const void *from = 0;
    uint16_t n = 0u;
    char detail[TEXT_CAP + 80];

    do {
        uint16_t room = (uint16_t)(TEXT_CAP - 1u - n);
        uint16_t got = heard_list(text + n, room, &from);

        if (got == 0u) {
            break;
        }
        n = (uint16_t)(n + got);
        if (from != 0 && n < (uint16_t)(TEXT_CAP - 1u)) {
            text[n] = (uint8_t)' ';
            ++n;
        }
    } while (from != 0 && n < (uint16_t)(TEXT_CAP - 1u));
    text[n] = '\0';
    if (strcmp((const char *)text, want) != 0) {
        snprintf(detail, sizeof detail, "heard \"%s\", want \"%s\"", (const char *)text, want);
        fail(name, detail);
        return;
    }
    ++passed;
}

static void hear(const char *in)
{
    uint8_t frame[FRAME_CAP];
    uint16_t len = 0u;

    tx_count = 0;
    if (encode(in, frame, &len) != 0) {
        fail(in, "could not build the test frame");
        return;
    }
    digi_ingress(frame, len);
}

static void test_heard(void)
{
    char text[64];
    char want[TEXT_CAP];
    int i;

    setup();
    set_nn(0, "WIDE");
    hear("N0AAA>APRS:Hi");
    expect_heard("no path is heard directly", "N0AAA");
    now = (uint16_t)(now + 1u);
    hear("N0BBB-7>APRS,WIDE1-1,WIDE2-1:Hi");
    expect_heard("fresh path is heard directly", "N0AAA N0BBB-7");
    now = (uint16_t)(now + 1u);
    hear("N0CCC>APRS,N0DIG*,WIDE2-1:Hi");
    hear("N0CCC>APRS,N0DIG*,WIDE2*:Hi");
    expect_heard("repeated path is not direct", "N0AAA N0BBB-7");
    hear("WFDIGI>APRS:Hi");
    expect_heard("own source is not listed", "N0AAA N0BBB-7");
    hear("WFDIGI-1>APRS:Hi");
    expect_heard("own call with another SSID is listed", "N0AAA N0BBB-7 WFDIGI-1");
    now = (uint16_t)(now + 1u);
    hear("N0AAA>APRS:Again");
    expect_heard("hearing again keeps its place", "N0AAA N0BBB-7 WFDIGI-1");

    setup();
    g_config.digipeat = 0u;
    hear("N0AAA>APRS,WIDE1-1:Hi");
    expect_heard("digipeat off still records", "N0AAA");
    now = (uint16_t)(now + 1u);
    check_keep("directed message sender is recorded", "N0SRC-9>APRS::WFDIGI   :Hello", 0);
    expect_heard("message sender is listed", "N0AAA N0SRC-9");

    setup();
    hear("N0OLD>APRS:Hi");
    now = (uint16_t)(now + 1u);
    hear("N0NEW>APRS:Hi");
    now = (uint16_t)(now + HEARD_MAX_AGE - 1u);
    expect_heard("8 hour age limit", "N0NEW");
    heard_sweep();
    now = (uint16_t)(now - 1u);
    expect_heard("sweep clears only expired entries", "N0NEW");
    now = (uint16_t)(now + 2u);
    heard_sweep();
    expect_heard("sweep clears the last entry", "");

    setup();
    for (i = 0; i < (int)HEARD_SLOTS; ++i) {
        snprintf(text, sizeof text, "N%dAA>APRS:Hi", i);
        hear(text);
        now = (uint16_t)(now + 1u);
    }
    hear("N0AA>APRS:Again");
    now = (uint16_t)(now + 1u);
    hear("N0NEW>APRS:Hi");
    want[0] = '\0';
    strcat(want, "N0AA N0NEW");
    for (i = 2; i < (int)HEARD_SLOTS; ++i) {
        snprintf(text, sizeof text, " N%dAA", i);
        strcat(want, text);
    }
    expect_heard("full list drops the oldest entry", want);

    setup();
    hear("N0AAA>APRS:Hi");
    now = (uint16_t)(now + 1u);
    check_keep("directs query", "N0SRC>APRS::WFDIGI   :?APRSD",
               "WFDIGI>APWFDP*::N0SRC    :Directs= N0AAA N0SRC");
    check_keep("directs query echo", "N0SRC>APRS::WFDIGI   :?APRSD", 0);
    now = (uint16_t)(now + 1u);
    check_two("numbered directs query", "N0OTH>APRS,WIDE1-1::WFDIGI   :?APRSD{2",
              "WFDIGI>APWFDP*::N0OTH    :ack2", "WFDIGI>APWFDP*::N0OTH    :Directs= N0AAA N0SRC N0OTH");

    setup();
    check_keep("directs query through a digipeater", "N0SRC>APRS,N0DIG*::WFDIGI   :?APRSD",
               "WFDIGI>APWFDP*::N0SRC    :Directs= ");

    setup();
    for (i = 0; i < 12; ++i) {
        snprintf(text, sizeof text, "N%02dAAA-15>APRS:Hi", i);
        hear(text);
        now = (uint16_t)(now + 1u);
    }
    {
        static const char *pages[] = {
            "WFDIGI>APWFDP*::N0SRC    :Directs= N00AAA-15 N01AAA-15 N02AAA-15 N03AAA-15 N04AAA-15",
            "WFDIGI>APWFDP*::N0SRC    :Directs= N05AAA-15 N06AAA-15 N07AAA-15 N08AAA-15 N09AAA-15",
            "WFDIGI>APWFDP*::N0SRC    :Directs= N10AAA-15 N11AAA-15",
        };

        check_frames("directs reply pages five calls at a time", "N0SRC>APRS,N0DIG*::WFDIGI   :?APRSD", pages, 3);
    }
    check_keep("directs pages are not repeated", "N0SRC>APRS,N0DIG*::WFDIGI   :?APRSD", 0);
}

static void test_droplist(void)
{
    setup();
    set_nn(0, "WIDE");
    set_drop(0, "N0BAD", 0u);
    check("drop listed source", "N0BAD>APRS,WIDE1-1:Hi", 0);
    check("other source is digipeated", "N0SRC>APRS,WIDE1-1:Hi", "N0SRC>APRS,WFDIGI*:Hi");

    setup();
    set_nn(0, "WIDE");
    set_drop(0, "N0BAD", 2u);
    check("ssid must match", "N0BAD>APRS,WIDE1-1:Hi", "N0BAD>APRS,WFDIGI*:Hi");
    check("matching ssid is dropped", "N0BAD-2>APRS,WIDE1-1:Hi", 0);

    setup();
    set_nn(0, "WIDE");
    set_drop(0, "N0BAD", SSID_ANY);
    check("wildcard matches ssid 0", "N0BAD>APRS,WIDE1-1:Hi", 0);
    check("wildcard matches ssid 15", "N0BAD-15>APRS,WIDE1-1:Hi", 0);

    setup();
    set_nn(0, "WIDE");
    set_drop(0, "N0BAD", 0u);
    check("unused via is not a used hop", "N0SRC>APRS,N0BAD,WIDE1-1:Hi", "N0SRC>APRS,N0BAD*,WFDIGI*:Hi");
    check("used via is dropped", "N0SRC>APRS,N0BAD*,WIDE1-1:Hi", 0);

    setup();
    set_nn(0, "WIDE");
    set_drop(0, "N0BAD", 0u);
    check_keep("drop listed message is not answered", "N0BAD>APRS::WFDIGI   :Hello{1", 0);
    expect_dupe_count("drop listed message leaves no ack", 0u);
    check_keep("drop listed station is still heard", "N0BAD>APRS:Hi", 0);
    expect_heard("drop listed source remains in heard list", "N0BAD");
}

int main(void)
{
    test_roundtrip();
    test_gates();
    test_ninth_via();
    test_mycall();
    test_alias();
    test_nnalias();
    test_directonly();
    test_maxhops();
    test_limits();
    test_dupe_count();
    test_dupes();
    test_viscous();
    test_messaging();
    test_heard();
    test_droplist();
    if (failed != 0) {
        printf("digi tests: %d passed, %d failed\n", passed, failed);
        return 1;
    }
    printf("digi tests: %d passed\n", passed);
    return 0;
}
