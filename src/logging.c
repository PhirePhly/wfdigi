#include "wfdigi.h"

/* AX.25 address block is 7 bytes. Destination, source, and up to 8 digipeaters. */
#define AX25_ADDR 7u
#define AX25_MAX_ADDRS 10u

/* Trace text waits here while the terminal catches up. A power of two keeps
 * the indices a mask. Near the top, logging stops until half the ring has
 * drained, and "!!!" marks the gap.
 */
#define TRACE_SIZE 256u
#define TRACE_HIGH (TRACE_SIZE - 16u)
#define TRACE_LOW 128u

_Static_assert((TRACE_SIZE & (TRACE_SIZE - 1u)) == 0u, "trace ring is a power of two");
_Static_assert(TRACE_HIGH + 3u <= TRACE_SIZE, "!!! fits before the trace ring is full");
_Static_assert(TRACE_LOW * 2u == TRACE_SIZE, "trace logging resumes at half");
_Static_assert(TRACE_LOW < TRACE_HIGH, "trace resume is below the shed point");

static char trace[TRACE_SIZE];
static uint8_t trace_head;
static uint8_t trace_tail;
static uint16_t trace_count;
/* Set while the ring is shedding. Cleared once the backlog is down to half. */
static uint8_t trace_shed;

static void trace_raw(uint8_t byte)
{
    if (trace_count >= TRACE_SIZE) {
        return;
    }
    trace[trace_head] = (char)byte;
    trace_head = (uint8_t)((trace_head + 1u) & (TRACE_SIZE - 1u));
    ++trace_count;
}

/* The ring is nearly full. Mark the gap and drop further trace text. */
static void trace_mark_full(void)
{
    trace_raw((uint8_t)'!');
    trace_raw((uint8_t)'!');
    trace_raw((uint8_t)'!');
    trace_shed = 1u;
}

static void trace_char(uint8_t byte)
{
    if (trace_shed != 0u) {
        return;
    }
    if (trace_count >= TRACE_HIGH) {
        trace_mark_full();
        return;
    }
    trace_raw(byte);
}

static void trace_puts(const char *text)
{
    while (*text != '\0') {
        trace_char((uint8_t)*text);
        ++text;
    }
}

static void trace_u16(uint16_t value, uint8_t width)
{
    uint8_t text[5];
    uint8_t n = format_u16(value, width, text);
    uint8_t i;

    for (i = 0u; i < n; ++i) {
        trace_char(text[i]);
    }
}

static void print_hex_byte(uint8_t value)
{
    uint8_t hi = (uint8_t)(value >> 4);
    uint8_t lo = (uint8_t)(value & 0x0Fu);

    trace_char((uint8_t)(hi < 10u ? '0' + hi : 'A' + (hi - 10u)));
    trace_char((uint8_t)(lo < 10u ? '0' + lo : 'A' + (lo - 10u)));
}

static void print_call(const uint8_t *raw)
{
    uint8_t i;
    uint8_t ssid;

    for (i = 0u; i < 6u; ++i) {
        uint8_t c = (uint8_t)((raw[i] >> 1) & 0x7Fu);
        if (c != ' ') {
            trace_char(c);
        }
    }
    ssid = (uint8_t)((raw[6] >> 1) & 0x0Fu);
    if (ssid != 0u) {
        trace_char('-');
        if (ssid >= 10u) {
            trace_char('1');
            ssid = (uint8_t)(ssid - 10u);
        }
        trace_char((uint8_t)('0' + ssid));
    }
}

static bool print_tnc2(const uint8_t *frame, uint16_t len)
{
    uint16_t at[AX25_MAX_ADDRS];
    uint8_t n = 0u;
    uint16_t i = 0u;
    uint8_t d;
    uint8_t last_h = 0u;
    bool saw_h = false;
    uint8_t ctl;

    while (n < AX25_MAX_ADDRS && (uint16_t)(i + AX25_ADDR) <= len) {
        at[n] = i;
        ++n;
        i = (uint16_t)(i + AX25_ADDR);
        if ((frame[i - 1u] & 0x01u) != 0u) {
            break;
        }
    }
    if (n < 2u || (frame[at[n - 1u] + 6u] & 0x01u) == 0u) {
        return false;
    }
    print_call(&frame[at[1]]);
    trace_char('>');
    print_call(&frame[at[0]]);
    for (d = 2u; d < n; ++d) {
        if ((frame[at[d] + 6u] & 0x80u) != 0u) {
            last_h = d;
            saw_h = true;
        }
    }
    for (d = 2u; d < n; ++d) {
        trace_char(',');
        print_call(&frame[at[d]]);
        if (saw_h && d == last_h) {
            trace_char('*');
        }
    }
    if (i >= len) {
        return true;
    }
    ctl = frame[i];
    ++i;
    trace_char(':');
    if (((ctl & 0x01u) == 0u) || ((ctl & 0xEFu) == 0x03u)) {
        if (i < len) {
            ++i;
        }
    }
    while (i < len) {
        uint8_t c;

        if (trace_shed != 0u) {
            return true;
        }
        c = frame[i];
        ++i;
        if (c >= 0x20u && c <= 0x7Eu) {
            trace_char(c);
        }
    }
    return true;
}

void logging_init(void)
{
    trace_head = 0u;
    trace_tail = 0u;
    trace_count = 0u;
    trace_shed = 0u;
}

void logging_service(void)
{
    if (trace_count == 0u) {
        trace_shed = 0u;
        return;
    }
    if (serial_try_putc((uint8_t)trace[trace_tail])) {
        trace_tail = (uint8_t)((trace_tail + 1u) & (TRACE_SIZE - 1u));
        --trace_count;
    }
    if (trace_shed != 0u && trace_count <= TRACE_LOW) {
        trace_shed = 0u;
    }
}

void logging_frame(char kind, const uint8_t *frame, uint16_t len)
{
    uint16_t i;
    uint8_t n;
    uint8_t c;

    if (g_config.logging == 0u || frame == 0 || trace_shed != 0u) {
        return;
    }
    trace_puts("\r\n");
    trace_char((uint8_t)kind);
    trace_char(' ');
    /* Five characters, space-padded, so the packet text lines up. */
    trace_u16(timer_seconds(), 5u);
    trace_char(' ');
    if (trace_shed != 0u) {
        return;
    }
    if (!print_tnc2(frame, len) && trace_shed == 0u) {
        for (i = 0u; i < len; ++i) {
            if (trace_shed != 0u) {
                return;
            }
            if (i != 0u) {
                trace_char(' ');
            }
            print_hex_byte(frame[i]);
        }
    }
    if (trace_shed != 0u) {
        return;
    }
    trace_puts("\r\n");
    trace_puts(cli_prompt());
    n = cli_pending_len();
    for (c = 0u; c < n; ++c) {
        trace_char((uint8_t)cli_pending_char(c));
    }
}
