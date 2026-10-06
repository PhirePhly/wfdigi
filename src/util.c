#include "wfdigi.h"

static const uint16_t u16_place[5] = {10000u, 1000u, 100u, 10u, 1u};

uint8_t format_u16(uint16_t value, uint8_t width, uint8_t *dest)
{
    bool leading = true;
    uint8_t n = 0u;
    uint8_t i;

    for (i = 0u; i < 5u; ++i) {
        uint16_t place = u16_place[i];
        uint8_t digit = 0u;
        bool last = i == 4u;

        while (value >= place) {
            value = (uint16_t)(value - place);
            ++digit;
        }
        if (leading && digit == 0u && !last) {
            if (width != 0u && (uint8_t)(5u - i) <= width) {
                dest[n] = ' ';
                ++n;
            }
        } else {
            dest[n] = (uint8_t)('0' + digit);
            ++n;
            leading = false;
        }
    }
    return n;
}

void print_u16(uint16_t value)
{
    uint8_t text[5];
    uint8_t n = format_u16(value, 0u, text);
    uint8_t i;

    for (i = 0u; i < n; ++i) {
        serial_putc(text[i]);
    }
}

uint8_t letter_to_upper(uint8_t c)
{
    if (c >= (uint8_t)'a' && c <= (uint8_t)'z') {
        c = (uint8_t)(c - (uint8_t)('a' - 'A'));
    }
    return c;
}

/* 32-bit xorshift (13, 17, 5), kept in two 16-bit halves. The top byte is the
 * result: the low bits of this generator are the weaker ones. State zero is
 * stuck, so a draw from zero reloads the seed.
 */
static uint16_t rng_lo;
static uint16_t rng_hi;

void prng_init(void)
{
    rng_lo = 0xA5C3u;
    rng_hi = 0x1E59u;
}

void prng_stir(uint16_t extra)
{
    rng_lo ^= extra;
    if (rng_lo == 0u && rng_hi == 0u) {
        rng_lo = 0xA5C3u;
    }
}

uint8_t prng_u8(void)
{
    uint16_t lo;
    uint16_t hi;
    uint16_t previous;

    if (rng_lo == 0u && rng_hi == 0u) {
        prng_init();
    }
    lo = rng_lo;
    hi = rng_hi;

    /* x ^= x << 13 */
    previous = lo;
    lo = (uint16_t)(lo ^ (uint16_t)(lo << 13));
    hi = (uint16_t)(hi ^ (uint16_t)(hi << 13) ^ (uint16_t)(previous >> 3));

    /* x ^= x >> 17 */
    lo = (uint16_t)(lo ^ (uint16_t)(hi >> 1));

    /* x ^= x << 5 */
    previous = lo;
    lo = (uint16_t)(lo ^ (uint16_t)(lo << 5));
    hi = (uint16_t)(hi ^ (uint16_t)(hi << 5) ^ (uint16_t)(previous >> 11));

    rng_lo = lo;
    rng_hi = hi;
    return (uint8_t)(hi >> 8);
}
