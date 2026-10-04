#include "wfdigi.h"
#include "config.h"

_Static_assert(sizeof(CFG_MYCALL) > 1u, "MYCALL is empty");
_Static_assert(sizeof(CFG_MYCALL) <= CALLSIGN_LEN + 1u, "MYCALL is longer than 6 characters");
_Static_assert(CFG_MYCALL_SSID <= SSID_MAX, "MYCALL SSID is above 15");
_Static_assert(CFG_DIGIPEAT <= 1u, "DIGIPEAT must be 0 or 1");
_Static_assert(CFG_DIRECTONLY <= 1u, "DIRECTONLY must be 0 or 1");
_Static_assert(CFG_TXDELAY >= TXDELAY_MIN && CFG_TXDELAY <= TXDELAY_MAX, "TXDELAY is out of range");
_Static_assert(CFG_PPERSIST >= PERSIST_MIN && CFG_PPERSIST <= PERSIST_MAX, "PPERSIST is out of range");
_Static_assert(CFG_SLOTTIME >= SLOTTIME_MIN && CFG_SLOTTIME <= SLOTTIME_MAX, "SLOTTIME is out of range");
_Static_assert(CFG_FULLDUP <= 1u, "FULLDUP must be 0 or 1");
_Static_assert(sizeof(CFG_ALIAS_0) <= CALLSIGN_LEN + 1u, "ALIAS 0 is longer than 6 characters");
_Static_assert(sizeof(CFG_ALIAS_1) <= CALLSIGN_LEN + 1u, "ALIAS 1 is longer than 6 characters");
_Static_assert(sizeof(CFG_ALIAS_2) <= CALLSIGN_LEN + 1u, "ALIAS 2 is longer than 6 characters");
_Static_assert(sizeof(CFG_ALIAS_3) <= CALLSIGN_LEN + 1u, "ALIAS 3 is longer than 6 characters");
_Static_assert(CFG_ALIAS_0_SSID <= SSID_MAX, "ALIAS 0 SSID is above 15");
_Static_assert(CFG_ALIAS_1_SSID <= SSID_MAX, "ALIAS 1 SSID is above 15");
_Static_assert(CFG_ALIAS_2_SSID <= SSID_MAX, "ALIAS 2 SSID is above 15");
_Static_assert(CFG_ALIAS_3_SSID <= SSID_MAX, "ALIAS 3 SSID is above 15");
_Static_assert(sizeof(CFG_NNALIAS_0) <= NNALIAS_LEN + 1u, "NNALIAS 0 is longer than 5 characters");
_Static_assert(sizeof(CFG_NNALIAS_1) <= NNALIAS_LEN + 1u, "NNALIAS 1 is longer than 5 characters");
_Static_assert(sizeof(CFG_NNALIAS_2) <= NNALIAS_LEN + 1u, "NNALIAS 2 is longer than 5 characters");
_Static_assert(sizeof(CFG_NNALIAS_3) <= NNALIAS_LEN + 1u, "NNALIAS 3 is longer than 5 characters");
_Static_assert(CFG_BEACON <= BEACON_MAX, "BEACON is above 60 minutes");
_Static_assert(sizeof(CFG_BTEXT) <= BTEXT_LEN, "BTEXT is longer than 63 characters");
_Static_assert(CFG_MAXHOPS >= MAXHOPS_MIN && CFG_MAXHOPS <= MAXHOPS_MAX, "MAXHOPS is out of range");
_Static_assert(sizeof(CFG_MYLOC) <= 32u, "MYLOC default is too long");
_Static_assert(sizeof(CFG_MYSYMBOL) == 3u, "MYSYMBOL must be two characters");
_Static_assert(CFG_LOGGING <= 1u, "LOGGING must be 0 or 1");

DigiConfig __at (CONFIG_ADDR) g_config;

bool tx_interlock;

void check_tx_interlock(void)
{
    tx_interlock = g_config.mycall[0] == (uint8_t)'N' &&
                   g_config.mycall[1] == (uint8_t)'0' &&
                   g_config.mycall[2] == (uint8_t)'C' &&
                   g_config.mycall[3] == (uint8_t)'A' &&
                   g_config.mycall[4] == (uint8_t)'L' &&
                   g_config.mycall[5] == (uint8_t)'L';
}

static bool g_ok;
/* False while cold boot is filling the image, so each store does not seal a partial page. */
static bool seal_live;

/* CRC-16/CCITT-FALSE: polynomial 0x1021, initial value 0xFFFF, no final XOR. */
static uint16_t crc16_feed(uint16_t crc, uint8_t byte)
{
    uint8_t bit;

    crc = (uint16_t)(crc ^ (uint16_t)((uint16_t)byte << 8));
    for (bit = 0u; bit < 8u; ++bit) {
        if ((crc & 0x8000u) != 0u) {
            crc = (uint16_t)((uint16_t)(crc << 1) ^ 0x1021u);
        } else {
            crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static uint16_t config_crc(void)
{
    uint8_t *raw = (uint8_t *)&g_config;
    uint16_t n = (uint16_t)sizeof(DigiConfig);
    uint16_t crc = 0xFFFFu;

    while (n != 0u) {
        crc = crc16_feed(crc, *raw);
        ++raw;
        --n;
    }
    return crc;
}

static uint8_t *config_crc_at(void)
{
    return (uint8_t *)&g_config + sizeof(DigiConfig);
}

static bool config_crc_ok(void)
{
    uint8_t *stored = config_crc_at();
    uint16_t found = (uint16_t)stored[0] | (uint16_t)((uint16_t)stored[1] << 8);

    return found == config_crc();
}

void config_seal(void)
{
    uint16_t crc = config_crc();
    uint8_t *stored = config_crc_at();

    stored[0] = (uint8_t)crc;
    stored[1] = (uint8_t)(crc >> 8);
}

static void note_config(void)
{
    if (seal_live) {
        config_seal();
    }
}

static void reject(const char *name)
{
    serial_puts("Bad config: ");
    serial_puts(name);
    serial_puts("\r\n");
    g_ok = false;
}

static void clear_config(void)
{
    uint8_t *raw = (uint8_t *)&g_config;
    uint8_t i;

    for (i = 0u; i < (uint8_t)sizeof(DigiConfig); ++i) {
        raw[i] = 0u;
    }
}

static bool callsign_ok(const uint8_t *call)
{
    uint8_t i;
    bool saw_space = false;
    bool any = false;

    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        uint8_t c = call[i];
        if (c == ' ') {
            saw_space = true;
            continue;
        }
        if (saw_space) {
            return false;
        }
        any = true;
        if (c >= '0' && c <= '9') {
            continue;
        }
        if (c >= 'A' && c <= 'Z') {
            continue;
        }
        return false;
    }
    return any;
}

static bool btext_ok(const uint8_t *text)
{
    uint8_t i;
    bool terminated = false;

    for (i = 0u; i < BTEXT_LEN; ++i) {
        uint8_t c = text[i];
        if (terminated) {
            if (c != 0u) {
                return false;
            }
            continue;
        }
        if (c == 0u) {
            terminated = true;
            continue;
        }
        if (c < 0x20u || c > 0x7Eu) {
            return false;
        }
    }
    return terminated;
}

static bool store_u8(const char *name, uint8_t value, uint8_t min, uint8_t max, uint8_t *dest)
{
    if (value < min || value > max) {
        reject(name);
        return false;
    }
    *dest = value;
    note_config();
    return true;
}

static void pad_call(uint8_t dest[CALLSIGN_LEN], const char *text)
{
    uint8_t i;
    bool ended = false;

    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        uint8_t c = ' ';
        if (!ended) {
            c = (uint8_t)text[i];
            if (c == 0u) {
                c = ' ';
                ended = true;
            }
        }
        dest[i] = c;
    }
}

static bool call_is_blank(const uint8_t *call)
{
    uint8_t i;

    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        if (call[i] != ' ') {
            return false;
        }
    }
    return true;
}

static bool store_call(const char *name, const char *text, uint8_t ssid, uint8_t *dest,
                       uint8_t *ssid_dest)
{
    uint8_t raw[CALLSIGN_LEN];
    uint8_t i;

    pad_call(raw, text);
    if (!callsign_ok(raw) || ssid > SSID_MAX) {
        reject(name);
        return false;
    }
    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        dest[i] = raw[i];
    }
    *ssid_dest = ssid;
    note_config();
    return true;
}

static bool store_alias(const char *text, uint8_t ssid, uint8_t *dest, uint8_t *ssid_dest)
{
    uint8_t raw[CALLSIGN_LEN];
    uint8_t i;

    pad_call(raw, text);
    if (call_is_blank(raw)) {
        for (i = 0u; i < CALLSIGN_LEN; ++i) {
            dest[i] = ' ';
        }
        *ssid_dest = 0u;
        note_config();
        return true;
    }
    return store_call("ALIAS", text, ssid, dest, ssid_dest);
}

static bool nnalias_ok(const uint8_t *name)
{
    uint8_t i;
    bool saw_space = false;

    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        uint8_t c = name[i];
        if (c == ' ') {
            saw_space = true;
            continue;
        }
        /* A sixth character would leave no room for the hop-limit digit. */
        if (saw_space || i >= NNALIAS_LEN) {
            return false;
        }
        if (c >= '0' && c <= '9') {
            continue;
        }
        if (c >= 'A' && c <= 'Z') {
            continue;
        }
        return false;
    }
    return true;
}

static bool store_nnalias(const char *text, uint8_t *dest)
{
    uint8_t raw[CALLSIGN_LEN];
    uint8_t i;

    pad_call(raw, text);
    if (call_is_blank(raw)) {
        for (i = 0u; i < CALLSIGN_LEN; ++i) {
            dest[i] = ' ';
        }
        note_config();
        return true;
    }
    if (!nnalias_ok(raw)) {
        reject("NNALIAS");
        return false;
    }
    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        dest[i] = raw[i];
    }
    note_config();
    return true;
}

static bool store_btext(const uint8_t *text)
{
    uint8_t i;

    if (!btext_ok(text)) {
        reject("BTEXT");
        return false;
    }
    for (i = 0u; i < BTEXT_LEN; ++i) {
        g_config.btext[i] = text[i];
    }
    note_config();
    return true;
}

static bool store_myloc(const char *text);
static bool store_symbol(const char *text);
static bool store_bpath(const char *text);
static void load_btext(void)
{
    uint8_t raw[BTEXT_LEN];
    uint8_t i;
    bool ended = false;

    for (i = 0u; i < BTEXT_LEN; ++i) {
        uint8_t c = 0u;
        if (!ended) {
            c = (uint8_t)CFG_BTEXT[i];
            if (c == 0u) {
                ended = true;
            }
        }
        raw[i] = c;
    }
    store_btext(raw);
}

static void config_cold_boot(void)
{
    serial_puts("Cold boot...\r\n");
    g_ok = true;
    clear_config();

    store_call("MYCALL", CFG_MYCALL, (uint8_t)CFG_MYCALL_SSID, g_config.mycall,
               &g_config.mycall_ssid);
    store_u8("DIGIPEAT", (uint8_t)CFG_DIGIPEAT, 0u, 1u, &g_config.digipeat);
    store_u8("DIRECTONLY", (uint8_t)CFG_DIRECTONLY, 0u, 1u, &g_config.directonly);
    store_u8("LOGGING", (uint8_t)CFG_LOGGING, 0u, 1u, &g_config.logging);
    store_u8("TXDELAY", (uint8_t)CFG_TXDELAY, TXDELAY_MIN, TXDELAY_MAX, &g_config.txdelay);
    store_u8("PPERSIST", (uint8_t)CFG_PPERSIST, PERSIST_MIN, PERSIST_MAX, &g_config.persist);
    store_u8("SLOTTIME", (uint8_t)CFG_SLOTTIME, SLOTTIME_MIN, SLOTTIME_MAX, &g_config.slottime);
    store_u8("FULLDUP", (uint8_t)CFG_FULLDUP, 0u, 1u, &g_config.fulldup);
    store_alias(CFG_ALIAS_0, (uint8_t)CFG_ALIAS_0_SSID, g_config.alias[0], &g_config.alias_ssid[0]);
    store_alias(CFG_ALIAS_1, (uint8_t)CFG_ALIAS_1_SSID, g_config.alias[1], &g_config.alias_ssid[1]);
    store_alias(CFG_ALIAS_2, (uint8_t)CFG_ALIAS_2_SSID, g_config.alias[2], &g_config.alias_ssid[2]);
    store_alias(CFG_ALIAS_3, (uint8_t)CFG_ALIAS_3_SSID, g_config.alias[3], &g_config.alias_ssid[3]);
    store_nnalias(CFG_NNALIAS_0, g_config.nnalias[0]);
    store_nnalias(CFG_NNALIAS_1, g_config.nnalias[1]);
    store_nnalias(CFG_NNALIAS_2, g_config.nnalias[2]);
    store_nnalias(CFG_NNALIAS_3, g_config.nnalias[3]);
    store_u8("BEACON", (uint8_t)CFG_BEACON, 0u, BEACON_MAX, &g_config.beacon_every);
    load_btext();
    store_bpath(CFG_BPATH);
    store_myloc(CFG_MYLOC);
    store_u8("MAXHOPS", (uint8_t)CFG_MAXHOPS, MAXHOPS_MIN, MAXHOPS_MAX, &g_config.maxhops);
    store_symbol(CFG_MYSYMBOL);

    g_config.valid = g_ok ? 1u : 0u;
    if (g_ok) {
        config_seal();
    }
}

void config_boot(void)
{
    seal_live = false;
    if (config_crc_ok()) {
        serial_puts("Warm boot...\r\n");
        g_config.bpath_next = 0u;
        config_seal();
    } else {
        config_cold_boot();
    }
    seal_live = true;
}

static bool same_text(const char *text, const char *word)
{
    uint8_t i = 0u;

    while (word[i] != '\0') {
        char c = text[i];
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - ('a' - 'A'));
        }
        if (c != word[i]) {
            return false;
        }
        ++i;
    }
    return text[i] == '\0';
}

static bool is_off(const char *text)
{
    return same_text(text, "OFF") || same_text(text, "-");
}

static bool accum_digit(uint8_t *value, uint8_t digit, uint8_t max)
{
    uint8_t times8;
    uint8_t times2;
    uint16_t sum;

    if (*value > 25u) {
        return false;
    }
    times8 = (uint8_t)(*value << 3);
    times2 = (uint8_t)(*value << 1);
    sum = (uint16_t)times8 + (uint16_t)times2 + (uint16_t)digit;
    if (sum > max) {
        return false;
    }
    *value = (uint8_t)sum;
    return true;
}

static bool parse_u8(const char *text, uint8_t *out, uint8_t max)
{
    uint8_t value = 0u;
    uint8_t i = 0u;

    if (text[0] == '\0') {
        return false;
    }
    while (text[i] != '\0') {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
        if (!accum_digit(&value, (uint8_t)(text[i] - '0'), max)) {
            return false;
        }
        ++i;
    }
    *out = value;
    return true;
}

static void show_flag(uint8_t value)
{
    serial_puts(value != 0u ? "ON\r\n" : "OFF\r\n");
}

static void show_call(const uint8_t *call, uint8_t ssid)
{
    uint8_t i;

    if (call_is_blank(call)) {
        serial_puts("OFF\r\n");
        return;
    }
    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        if (call[i] == ' ') {
            break;
        }
        serial_putc(call[i]);
    }
    serial_putc('-');
    print_u16(ssid);
    serial_puts("\r\n");
}

static void show_prefix(const uint8_t *name)
{
    uint8_t i;

    if (call_is_blank(name)) {
        serial_puts("OFF\r\n");
        return;
    }
    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        if (name[i] == ' ') {
            break;
        }
        serial_putc(name[i]);
    }
    serial_puts("\r\n");
}

static void show_btext(void)
{
    if (g_config.btext[0] == 0u) {
        serial_puts("-\r\n");
        return;
    }
    serial_puts((const char *)g_config.btext);
    serial_puts("\r\n");
}

static void upper_inplace(char *text)
{
    while (*text != '\0') {
        if (*text >= 'a' && *text <= 'z') {
            *text = (char)(*text - ('a' - 'A'));
        }
        ++text;
    }
}

static bool parse_call(char *text, char *body, uint8_t *ssid)
{
    uint8_t i = 0u;
    uint8_t n = 0u;
    uint8_t ss = 0u;
    bool saw_dash = false;
    bool saw_digit = false;

    upper_inplace(text);
    while (text[i] != '\0') {
        char c = text[i];
        ++i;
        if (!saw_dash) {
            if (c == '-') {
                saw_dash = true;
                continue;
            }
            if (n >= CALLSIGN_LEN) {
                return false;
            }
            body[n] = c;
            ++n;
            continue;
        }
        if (c < '0' || c > '9') {
            return false;
        }
        saw_digit = true;
        if (!accum_digit(&ss, (uint8_t)(c - '0'), SSID_MAX)) {
            return false;
        }
    }
    if (n == 0u || (saw_dash && !saw_digit)) {
        return false;
    }
    body[n] = '\0';
    *ssid = ss;
    return true;
}

static bool set_flag(const char *name, const char *value, uint8_t *dest)
{
    uint8_t flag;

    if (same_text(value, "ON") || same_text(value, "1")) {
        flag = 1u;
    } else if (same_text(value, "OFF") || same_text(value, "0")) {
        flag = 0u;
    } else {
        reject(name);
        return false;
    }
    *dest = flag;
    note_config();
    return true;
}

static bool set_number(const char *name, const char *value, uint8_t min, uint8_t max, uint8_t *dest)
{
    uint8_t number;

    if (!parse_u8(value, &number, max) || number < min) {
        reject(name);
        return false;
    }
    *dest = number;
    note_config();
    return true;
}

static void command_call(const char *name, char *value, uint8_t *call, uint8_t *ssid, bool alias)
{
    char body[CALLSIGN_LEN + 1u];
    uint8_t new_ssid = 0u;

    if (value == 0) {
        show_call(call, *ssid);
        return;
    }
    if (alias && is_off(value)) {
        if (store_alias("", 0u, call, ssid)) {
            show_call(call, *ssid);
        }
        return;
    }
    if (!parse_call(value, body, &new_ssid)) {
        reject(name);
        return;
    }
    if (alias) {
        if (store_alias(body, new_ssid, call, ssid)) {
            show_call(call, *ssid);
        }
        return;
    }
    if (store_call(name, body, new_ssid, call, ssid)) {
        show_call(call, *ssid);
    }
}

static void command_prefix(char *value, uint8_t *dest)
{
    if (value == 0) {
        show_prefix(dest);
        return;
    }
    if (is_off(value)) {
        if (store_nnalias("", dest)) {
            show_prefix(dest);
        }
        return;
    }
    upper_inplace(value);
    if (store_nnalias(value, dest)) {
        show_prefix(dest);
    }
}

static void command_btext(const char *value)
{
    uint8_t raw[BTEXT_LEN];
    uint8_t i;
    bool ended = false;

    if (value == 0) {
        show_btext();
        return;
    }
    if (value[0] == '-' && value[1] == '\0') {
        raw[0] = 0u;
        for (i = 1u; i < BTEXT_LEN; ++i) {
            raw[i] = 0u;
        }
        if (store_btext(raw)) {
            show_btext();
        }
        return;
    }
    for (i = 0u; i < BTEXT_LEN; ++i) {
        uint8_t c = 0u;
        if (!ended) {
            c = (uint8_t)value[i];
            if (c == 0u) {
                ended = true;
            }
        }
        raw[i] = c;
    }
    if (!ended) {
        reject("BTEXT");
        return;
    }
    if (store_btext(raw)) {
        show_btext();
    }
}

typedef struct {
    uint8_t lat_deg;
    uint8_t lat_min;
    uint8_t lat_hund;
    uint8_t lat_ns;
    uint8_t lon_deg;
    uint8_t lon_min;
    uint8_t lon_hund;
    uint8_t lon_ew;
} LocFields;

static bool take_spaces(const char **text)
{
    if (**text != ' ') {
        return false;
    }
    while (**text == ' ') {
        ++(*text);
    }
    return true;
}

static bool parse_span_u8(const char **text, uint8_t *out, uint8_t max)
{
    uint8_t value = 0u;

    if (**text < '0' || **text > '9') {
        return false;
    }
    while (**text >= '0' && **text <= '9') {
        if (!accum_digit(&value, (uint8_t)(**text - '0'), max)) {
            return false;
        }
        ++(*text);
    }
    *out = value;
    return true;
}

static uint8_t digit_times10(uint8_t digit)
{
    return (uint8_t)((digit << 3) + (digit << 1));
}

static bool parse_minutes(const char **text, uint8_t *whole, uint8_t *hund)
{
    uint8_t w;
    uint8_t frac = 0u;

    if (!parse_span_u8(text, &w, 59u)) {
        return false;
    }
    if (**text == '.') {
        uint8_t digit;

        ++(*text);
        if (**text < '0' || **text > '9') {
            return false;
        }
        digit = (uint8_t)(**text - '0');
        ++(*text);
        frac = digit_times10(digit);
        if (**text >= '0' && **text <= '9') {
            frac = (uint8_t)(frac + (uint8_t)(**text - '0'));
            ++(*text);
            if (**text >= '0' && **text <= '9') {
                return false;
            }
        }
    }
    *whole = w;
    *hund = frac;
    return true;
}

static bool parse_hemi(const char **text, uint8_t *out, char pos, char neg)
{
    char c = **text;

    if (c >= 'a' && c <= 'z') {
        c = (char)(c - ('a' - 'A'));
    }
    if (c != pos && c != neg) {
        return false;
    }
    ++(*text);
    *out = (uint8_t)c;
    return true;
}

static bool parse_myloc(const char *text, LocFields *loc)
{
    if (!parse_span_u8(&text, &loc->lat_deg, 90u) || !take_spaces(&text)) {
        return false;
    }
    if (!parse_minutes(&text, &loc->lat_min, &loc->lat_hund) || !take_spaces(&text)) {
        return false;
    }
    if (loc->lat_deg == 90u && (loc->lat_min != 0u || loc->lat_hund != 0u)) {
        return false;
    }
    if (!parse_hemi(&text, &loc->lat_ns, 'N', 'S') || !take_spaces(&text)) {
        return false;
    }
    if (!parse_span_u8(&text, &loc->lon_deg, 180u) || !take_spaces(&text)) {
        return false;
    }
    if (!parse_minutes(&text, &loc->lon_min, &loc->lon_hund) || !take_spaces(&text)) {
        return false;
    }
    if (loc->lon_deg == 180u && (loc->lon_min != 0u || loc->lon_hund != 0u)) {
        return false;
    }
    if (!parse_hemi(&text, &loc->lon_ew, 'E', 'W')) {
        return false;
    }
    while (*text == ' ') {
        ++text;
    }
    return *text == '\0';
}

static void print_2(uint8_t value)
{
    uint8_t tens = 0u;

    while (value >= 10u) {
        value = (uint8_t)(value - 10u);
        ++tens;
    }
    serial_putc((uint8_t)('0' + tens));
    serial_putc((uint8_t)('0' + value));
}

static void print_3(uint8_t value)
{
    uint8_t hundreds = 0u;

    while (value >= 100u) {
        value = (uint8_t)(value - 100u);
        ++hundreds;
    }
    serial_putc((uint8_t)('0' + hundreds));
    print_2(value);
}

static void show_myloc(void)
{
    print_2(g_config.loc_lat_deg);
    serial_putc(' ');
    print_2(g_config.loc_lat_min);
    serial_putc('.');
    print_2(g_config.loc_lat_hund);
    serial_putc(' ');
    serial_putc(g_config.loc_lat_ns);
    serial_putc(' ');
    print_3(g_config.loc_lon_deg);
    serial_putc(' ');
    print_2(g_config.loc_lon_min);
    serial_putc('.');
    print_2(g_config.loc_lon_hund);
    serial_putc(' ');
    serial_putc(g_config.loc_lon_ew);
    serial_puts("\r\n");
}

static bool store_myloc(const char *text)
{
    LocFields loc;

    if (!parse_myloc(text, &loc)) {
        reject("MYLOC");
        return false;
    }
    g_config.loc_lat_deg = loc.lat_deg;
    g_config.loc_lat_min = loc.lat_min;
    g_config.loc_lat_hund = loc.lat_hund;
    g_config.loc_lat_ns = loc.lat_ns;
    g_config.loc_lon_deg = loc.lon_deg;
    g_config.loc_lon_min = loc.lon_min;
    g_config.loc_lon_hund = loc.lon_hund;
    g_config.loc_lon_ew = loc.lon_ew;
    note_config();
    return true;
}

static bool symbol_ok(uint8_t table, uint8_t code)
{
    if (code < '!' || code > '~') {
        return false;
    }
    if (table == '/' || table == '\\') {
        return true;
    }
    if (table >= '0' && table <= '9') {
        return true;
    }
    return table >= 'A' && table <= 'Z';
}

static void show_symbol(void)
{
    serial_putc(g_config.symbol_table);
    serial_putc(g_config.symbol_code);
    serial_puts("\r\n");
}

static bool store_symbol(const char *text)
{
    uint8_t table;
    uint8_t code;

    if (text[0] == '\0' || text[1] == '\0' || text[2] != '\0') {
        reject("MYSYMBOL");
        return false;
    }
    table = (uint8_t)text[0];
    code = (uint8_t)text[1];
    if (table >= 'a' && table <= 'z') {
        table = (uint8_t)(table - ('a' - 'A'));
    }
    if (!symbol_ok(table, code)) {
        reject("MYSYMBOL");
        return false;
    }
    g_config.symbol_table = table;
    g_config.symbol_code = code;
    note_config();
    return true;
}

static void print_path(const uint8_t *call, uint8_t ssid)
{
    uint8_t i;

    if (call_is_blank(call)) {
        serial_putc('-');
        return;
    }
    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        if (call[i] == ' ') {
            break;
        }
        serial_putc(call[i]);
    }
    serial_putc('-');
    print_u16(ssid);
}

static void show_bpath(void)
{
    uint8_t i;
    uint8_t count = g_config.bpath_count;

    if (count == 0u || count > BPATH_SLOTS) {
        serial_puts("-\r\n");
        return;
    }
    for (i = 0u; i < count; ++i) {
        if (i != 0u) {
            serial_putc(' ');
        }
        print_path(g_config.bpath[i], g_config.bpath_ssid[i]);
    }
    serial_puts("\r\n");
}

static bool store_bpath(const char *text)
{
    uint8_t calls[BPATH_SLOTS][CALLSIGN_LEN];
    uint8_t ssids[BPATH_SLOTS];
    uint8_t count = 0u;
    uint8_t i;

    while (*text != '\0') {
        char token[CALLSIGN_LEN + 4u];
        uint8_t n = 0u;
        char body[CALLSIGN_LEN + 1u];
        uint8_t ssid = 0u;

        if (count >= BPATH_SLOTS) {
            reject("BPATH");
            return false;
        }
        while (*text != '\0' && *text != ' ') {
            if (n + 1u >= (uint8_t)sizeof(token)) {
                reject("BPATH");
                return false;
            }
            token[n] = *text;
            ++n;
            ++text;
        }
        token[n] = '\0';
        if (n == 1u && token[0] == '-') {
            pad_call(calls[count], "");
            ssids[count] = 0u;
        } else {
            if (!parse_call(token, body, &ssid)) {
                reject("BPATH");
                return false;
            }
            pad_call(calls[count], body);
            if (!callsign_ok(calls[count])) {
                reject("BPATH");
                return false;
            }
            ssids[count] = ssid;
        }
        ++count;
        while (*text == ' ') {
            ++text;
        }
    }
    if (count == 0u) {
        reject("BPATH");
        return false;
    }
    for (i = 0u; i < BPATH_SLOTS; ++i) {
        uint8_t c;
        if (i < count) {
            for (c = 0u; c < CALLSIGN_LEN; ++c) {
                g_config.bpath[i][c] = calls[i][c];
            }
            g_config.bpath_ssid[i] = ssids[i];
        } else {
            for (c = 0u; c < CALLSIGN_LEN; ++c) {
                g_config.bpath[i][c] = ' ';
            }
            g_config.bpath_ssid[i] = 0u;
        }
    }
    g_config.bpath_count = count;
    g_config.bpath_next = 0u;
    note_config();
    return true;
}

static void show_number(const char *name, uint8_t value)
{
    serial_puts(name);
    serial_putc(' ');
    print_u16(value);
    serial_puts("\r\n");
}

static void show_beacon(void)
{
    if (g_config.beacon_every == 0u) {
        serial_puts("OFF\r\n");
    } else {
        print_u16(g_config.beacon_every);
        serial_puts("\r\n");
    }
}

static void command_display(void)
{
    serial_puts("MYCALL ");
    show_call(g_config.mycall, g_config.mycall_ssid);
    serial_puts("DIGIPEAT ");
    show_flag(g_config.digipeat);
    serial_puts("DIRECTONLY ");
    show_flag(g_config.directonly);
    serial_puts("LOGGING ");
    show_flag(g_config.logging);
    show_number("TXDELAY", g_config.txdelay);
    show_number("PPERSIST", g_config.persist);
    show_number("SLOTTIME", g_config.slottime);
    serial_puts("FULLDUP ");
    show_flag(g_config.fulldup);
    serial_puts("ALIAS0 ");
    show_call(g_config.alias[0], g_config.alias_ssid[0]);
    serial_puts("ALIAS1 ");
    show_call(g_config.alias[1], g_config.alias_ssid[1]);
    serial_puts("ALIAS2 ");
    show_call(g_config.alias[2], g_config.alias_ssid[2]);
    serial_puts("ALIAS3 ");
    show_call(g_config.alias[3], g_config.alias_ssid[3]);
    serial_puts("NNALIAS0 ");
    show_prefix(g_config.nnalias[0]);
    serial_puts("NNALIAS1 ");
    show_prefix(g_config.nnalias[1]);
    serial_puts("NNALIAS2 ");
    show_prefix(g_config.nnalias[2]);
    serial_puts("NNALIAS3 ");
    show_prefix(g_config.nnalias[3]);
    serial_puts("BEACON ");
    show_beacon();
    serial_puts("BTEXT ");
    show_btext();
    serial_puts("BPATH ");
    show_bpath();
    serial_puts("MYLOC ");
    show_myloc();
    show_number("MAXHOPS", g_config.maxhops);
    serial_puts("MYSYMBOL ");
    show_symbol();
}

static void command_cal(char *value)
{
    char mode;
    uint8_t i;
    uint8_t seconds;
    uint8_t tone;

    if (value == 0 || value[0] == '\0' || value[1] != ' ') {
        serial_puts("?\r\n");
        return;
    }
    mode = value[0];
    if (mode >= 'a' && mode <= 'z') {
        mode = (char)(mode - ('a' - 'A'));
    }
    if (mode == 'H') {
        tone = CAL_HIGH;
    } else if (mode == 'L') {
        tone = CAL_LOW;
    } else if (mode == 'D') {
        tone = CAL_BOTH;
    } else {
        serial_puts("?\r\n");
        return;
    }
    i = 2u;
    while (value[i] == ' ') {
        ++i;
    }
    if (!parse_u8(&value[i], &seconds, CAL_SECONDS_MAX) || seconds == 0u) {
        serial_puts("Bad config: CAL\r\n");
        return;
    }
    modem_calibrate(tone, seconds);
}

void config_command(char *line)
{
    char *cmd;
    char *value;
    uint8_t i = 0u;
    uint8_t end;

    while (line[i] == ' ') {
        ++i;
    }
    if (line[i] == '\0') {
        return;
    }
    cmd = &line[i];
    while (line[i] != '\0' && line[i] != ' ') {
        if (line[i] >= 'a' && line[i] <= 'z') {
            line[i] = (char)(line[i] - ('a' - 'A'));
        }
        ++i;
    }
    if (line[i] == ' ') {
        line[i] = '\0';
        ++i;
        while (line[i] == ' ') {
            ++i;
        }
        value = &line[i];
        end = 0u;
        while (value[end] != '\0') {
            ++end;
        }
        while (end > 0u && value[end - 1u] == ' ') {
            --end;
            value[end] = '\0';
        }
        if (value[0] == '\0') {
            value = 0;
        }
    } else {
        value = 0;
    }

    if (same_text(cmd, "HELP")) {
        if (value == 0) {
            serial_puts("Visit https://github.com/PhirePhly/wfdigi for documentation\r\n");
        } else {
            serial_puts("?\r\n");
        }
        return;
    }
    if (same_text(cmd, "DISPLAY")) {
        if (value == 0) {
            command_display();
        } else {
            serial_puts("?\r\n");
        }
        return;
    }
    if (same_text(cmd, "ENGSTAT")) {
        if (value == 0) {
            engine_stat();
        } else {
            serial_puts("?\r\n");
        }
        return;
    }
    if (same_text(cmd, "MYCALL")) {
        command_call("MYCALL", value, g_config.mycall, &g_config.mycall_ssid, false);
        return;
    }
    if (same_text(cmd, "DIGIPEAT")) {
        if (value == 0) {
            show_flag(g_config.digipeat);
        } else if (set_flag("DIGIPEAT", value, &g_config.digipeat)) {
            show_flag(g_config.digipeat);
        }
        return;
    }
    if (same_text(cmd, "DIRECTONLY")) {
        if (value == 0) {
            show_flag(g_config.directonly);
        } else if (set_flag("DIRECTONLY", value, &g_config.directonly)) {
            show_flag(g_config.directonly);
        }
        return;
    }
    if (same_text(cmd, "LOGGING")) {
        if (value == 0) {
            show_flag(g_config.logging);
        } else if (set_flag("LOGGING", value, &g_config.logging)) {
            show_flag(g_config.logging);
        }
        return;
    }
    if (same_text(cmd, "TXDELAY")) {
        if (value == 0) {
            print_u16(g_config.txdelay);
            serial_puts("\r\n");
        } else if (set_number("TXDELAY", value, TXDELAY_MIN, TXDELAY_MAX, &g_config.txdelay)) {
            print_u16(g_config.txdelay);
            serial_puts("\r\n");
        }
        return;
    }
    if (same_text(cmd, "PPERSIST")) {
        if (value == 0) {
            print_u16(g_config.persist);
            serial_puts("\r\n");
        } else if (set_number("PPERSIST", value, PERSIST_MIN, PERSIST_MAX, &g_config.persist)) {
            print_u16(g_config.persist);
            serial_puts("\r\n");
        }
        return;
    }
    if (same_text(cmd, "SLOTTIME")) {
        if (value == 0) {
            print_u16(g_config.slottime);
            serial_puts("\r\n");
        } else if (set_number("SLOTTIME", value, SLOTTIME_MIN, SLOTTIME_MAX, &g_config.slottime)) {
            print_u16(g_config.slottime);
            serial_puts("\r\n");
        }
        return;
    }
    if (same_text(cmd, "FULLDUP")) {
        if (value == 0) {
            show_flag(g_config.fulldup);
        } else if (set_flag("FULLDUP", value, &g_config.fulldup)) {
            show_flag(g_config.fulldup);
        }
        return;
    }
    if (same_text(cmd, "ALIAS0")) {
        command_call("ALIAS", value, g_config.alias[0], &g_config.alias_ssid[0], true);
        return;
    }
    if (same_text(cmd, "ALIAS1")) {
        command_call("ALIAS", value, g_config.alias[1], &g_config.alias_ssid[1], true);
        return;
    }
    if (same_text(cmd, "ALIAS2")) {
        command_call("ALIAS", value, g_config.alias[2], &g_config.alias_ssid[2], true);
        return;
    }
    if (same_text(cmd, "ALIAS3")) {
        command_call("ALIAS", value, g_config.alias[3], &g_config.alias_ssid[3], true);
        return;
    }
    if (same_text(cmd, "NNALIAS0")) {
        command_prefix(value, g_config.nnalias[0]);
        return;
    }
    if (same_text(cmd, "NNALIAS1")) {
        command_prefix(value, g_config.nnalias[1]);
        return;
    }
    if (same_text(cmd, "NNALIAS2")) {
        command_prefix(value, g_config.nnalias[2]);
        return;
    }
    if (same_text(cmd, "NNALIAS3")) {
        command_prefix(value, g_config.nnalias[3]);
        return;
    }
    if (same_text(cmd, "BSEND")) {
        if (value != 0) {
            serial_puts("?\r\n");
        } else if (!timer_beacon_now()) {
            serial_puts("Busy\r\n");
        }
        return;
    }
    if (same_text(cmd, "BEACON")) {
        if (value == 0) {
            show_beacon();
        } else if (is_off(value)) {
            g_config.beacon_every = 0u;
            note_config();
            timer_beacon_restart();
            show_beacon();
        } else if (set_number("BEACON", value, 0u, BEACON_MAX, &g_config.beacon_every)) {
            timer_beacon_restart();
            show_beacon();
        }
        return;
    }
    if (same_text(cmd, "BTEXT")) {
        command_btext(value);
        return;
    }
    if (same_text(cmd, "BPATH")) {
        if (value == 0) {
            show_bpath();
        } else if (store_bpath(value)) {
            show_bpath();
        }
        return;
    }
    if (same_text(cmd, "MYLOC")) {
        if (value == 0) {
            show_myloc();
        } else if (store_myloc(value)) {
            show_myloc();
        }
        return;
    }
    if (same_text(cmd, "MAXHOPS")) {
        if (value == 0) {
            print_u16(g_config.maxhops);
            serial_puts("\r\n");
        } else if (set_number("MAXHOPS", value, MAXHOPS_MIN, MAXHOPS_MAX, &g_config.maxhops)) {
            print_u16(g_config.maxhops);
            serial_puts("\r\n");
        }
        return;
    }
    if (same_text(cmd, "MYSYMBOL")) {
        if (value == 0) {
            show_symbol();
        } else if (store_symbol(value)) {
            show_symbol();
        }
        return;
    }
    if (same_text(cmd, "CAL")) {
        command_cal(value);
        return;
    }
    serial_puts("Huh?\r\n");
}
