#include "wfdigi.h"

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
