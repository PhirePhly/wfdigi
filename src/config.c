#include "pk88.h"
#include "config.h"

_Static_assert(sizeof(CFG_MYCALL) > 1u, "MYCALL is empty");
_Static_assert(sizeof(CFG_MYCALL) <= CALLSIGN_LEN + 1u, "MYCALL is longer than 6 characters");
_Static_assert(CFG_MYCALL_SSID <= SSID_MAX, "MYCALL SSID is above 15");
_Static_assert(CFG_DIGIPEAT <= 1u, "DIGIPEAT must be 0 or 1");
_Static_assert(CFG_TXDELAY >= TXDELAY_MIN && CFG_TXDELAY <= TXDELAY_MAX, "TXDELAY is out of range");
_Static_assert(CFG_PERSIST >= PERSIST_MIN && CFG_PERSIST <= PERSIST_MAX, "PERSIST is out of range");
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

DigiConfig __at (CONFIG_ADDR) g_config;

static bool g_ok;

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
        return true;
    }
    if (!nnalias_ok(raw)) {
        reject("NNALIAS");
        return false;
    }
    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        dest[i] = raw[i];
    }
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
    return true;
}

static bool store_myloc(const char *text);
static bool store_symbol(const char *text);
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

void config_cold_boot(void)
{
    serial_puts("Cold boot...\r\n");
    g_ok = true;
    clear_config();

    store_call("MYCALL", CFG_MYCALL, (uint8_t)CFG_MYCALL_SSID, g_config.mycall,
               &g_config.mycall_ssid);
    store_u8("DIGIPEAT", (uint8_t)CFG_DIGIPEAT, 0u, 1u, &g_config.digipeat);
    store_u8("TXDELAY", (uint8_t)CFG_TXDELAY, TXDELAY_MIN, TXDELAY_MAX, &g_config.txdelay);
    store_u8("PERSIST", (uint8_t)CFG_PERSIST, PERSIST_MIN, PERSIST_MAX, &g_config.persist);
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
    store_myloc(CFG_MYLOC);
    store_u8("MAXHOPS", (uint8_t)CFG_MAXHOPS, MAXHOPS_MIN, MAXHOPS_MAX, &g_config.maxhops);
    store_symbol(CFG_MYSYMBOL);

    g_config.valid = g_ok ? 1u : 0u;
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

static void print_u8(uint8_t value)
{
    uint8_t hundreds = 0u;
    uint8_t tens = 0u;

    while (value >= 100u) {
        value = (uint8_t)(value - 100u);
        ++hundreds;
    }
    while (value >= 10u) {
        value = (uint8_t)(value - 10u);
        ++tens;
    }
    if (hundreds != 0u) {
        serial_putc((uint8_t)('0' + hundreds));
    }
    if (hundreds != 0u || tens != 0u) {
        serial_putc((uint8_t)('0' + tens));
    }
    serial_putc((uint8_t)('0' + value));
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
    print_u8(ssid);
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

static void print_hund(uint8_t hund)
{
    uint8_t tenths = 0u;

    while (hund >= 10u) {
        hund = (uint8_t)(hund - 10u);
        ++tenths;
    }
    serial_putc('.');
    serial_putc((uint8_t)('0' + tenths));
    if (hund != 0u) {
        serial_putc((uint8_t)('0' + hund));
    }
}

static void show_myloc(void)
{
    print_u8(g_config.loc_lat_deg);
    serial_putc(' ');
    print_u8(g_config.loc_lat_min);
    print_hund(g_config.loc_lat_hund);
    serial_putc(' ');
    serial_putc(g_config.loc_lat_ns);
    serial_putc(' ');
    print_u8(g_config.loc_lon_deg);
    serial_putc(' ');
    print_u8(g_config.loc_lon_min);
    print_hund(g_config.loc_lon_hund);
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
    return true;
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
    if (same_text(cmd, "TXDELAY")) {
        if (value == 0) {
            print_u8(g_config.txdelay);
            serial_puts("\r\n");
        } else if (set_number("TXDELAY", value, TXDELAY_MIN, TXDELAY_MAX, &g_config.txdelay)) {
            print_u8(g_config.txdelay);
            serial_puts("\r\n");
        }
        return;
    }
    if (same_text(cmd, "PERSIST")) {
        if (value == 0) {
            print_u8(g_config.persist);
            serial_puts("\r\n");
        } else if (set_number("PERSIST", value, PERSIST_MIN, PERSIST_MAX, &g_config.persist)) {
            print_u8(g_config.persist);
            serial_puts("\r\n");
        }
        return;
    }
    if (same_text(cmd, "SLOTTIME")) {
        if (value == 0) {
            print_u8(g_config.slottime);
            serial_puts("\r\n");
        } else if (set_number("SLOTTIME", value, SLOTTIME_MIN, SLOTTIME_MAX, &g_config.slottime)) {
            print_u8(g_config.slottime);
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
    if (same_text(cmd, "BEACON")) {
        if (value == 0) {
            print_u8(g_config.beacon_every);
            serial_puts("\r\n");
        } else if (is_off(value)) {
            g_config.beacon_every = 0u;
            print_u8(0u);
            serial_puts("\r\n");
        } else if (set_number("BEACON", value, 0u, BEACON_MAX, &g_config.beacon_every)) {
            print_u8(g_config.beacon_every);
            serial_puts("\r\n");
        }
        return;
    }
    if (same_text(cmd, "BTEXT")) {
        command_btext(value);
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
            print_u8(g_config.maxhops);
            serial_puts("\r\n");
        } else if (set_number("MAXHOPS", value, MAXHOPS_MIN, MAXHOPS_MAX, &g_config.maxhops)) {
            print_u8(g_config.maxhops);
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
    serial_puts("?\r\n");
}
