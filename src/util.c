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

/* The address callsign is each character shifted left by one. The SSID is
 * the low four bits of the shifted seventh byte.
 */
bool call_eq(const uint8_t *ax, const uint8_t *call, uint8_t ssid)
{
    uint8_t i;
    uint8_t got;

    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        if (ax[i] != (uint8_t)(call[i] << 1)) {
            return false;
        }
    }
    got = (uint8_t)((ax[6] >> 1) & 0x0Fu);
    return got == ssid;
}

/* Permutation of 0..255. The Pearson step is table[hash XOR byte], so the
 * order of the bytes changes the result.
 */
static const uint8_t pearson_table[256] = {
    0x62u, 0x06u, 0x55u, 0x96u, 0x24u, 0x17u, 0x70u, 0xA4u,
    0x87u, 0xCFu, 0xA9u, 0x05u, 0x1Au, 0x40u, 0xA5u, 0xDBu,
    0x3Du, 0x14u, 0x44u, 0x59u, 0x82u, 0x3Fu, 0x34u, 0x66u,
    0x18u, 0xE5u, 0x84u, 0xF5u, 0x50u, 0xD8u, 0xC3u, 0x73u,
    0x5Au, 0xA8u, 0x9Cu, 0xCBu, 0xB1u, 0x78u, 0x02u, 0xBEu,
    0xBCu, 0x07u, 0x64u, 0xB9u, 0xAEu, 0xF3u, 0xA2u, 0x0Au,
    0xEDu, 0x12u, 0xFDu, 0xE1u, 0x08u, 0xD0u, 0xACu, 0xF4u,
    0xFFu, 0x7Eu, 0x65u, 0x4Fu, 0x91u, 0xEBu, 0xE4u, 0x79u,
    0x7Bu, 0xFBu, 0x43u, 0xFAu, 0xA1u, 0x00u, 0x6Bu, 0x61u,
    0xF1u, 0x6Fu, 0xB5u, 0x52u, 0xF9u, 0x21u, 0x45u, 0x37u,
    0x3Bu, 0x99u, 0x1Du, 0x09u, 0xD5u, 0xA7u, 0x54u, 0x5Du,
    0x1Eu, 0x2Eu, 0x5Eu, 0x4Bu, 0x97u, 0x72u, 0x49u, 0xDEu,
    0xC5u, 0x60u, 0xD2u, 0x2Du, 0x10u, 0xE3u, 0xF8u, 0xCAu,
    0x33u, 0x98u, 0xFCu, 0x7Du, 0x51u, 0xCEu, 0xD7u, 0xBAu,
    0x27u, 0x9Eu, 0xB2u, 0xBBu, 0x83u, 0x88u, 0x01u, 0x31u,
    0x32u, 0x11u, 0x8Du, 0x5Bu, 0x2Fu, 0x81u, 0x3Cu, 0x63u,
    0x9Au, 0x23u, 0x56u, 0xABu, 0x69u, 0x22u, 0x26u, 0xC8u,
    0x93u, 0x3Au, 0x4Du, 0x76u, 0xADu, 0xF6u, 0x4Cu, 0xFEu,
    0x85u, 0xE8u, 0xC4u, 0x90u, 0xC6u, 0x7Cu, 0x35u, 0x04u,
    0x6Cu, 0x4Au, 0xDFu, 0xEAu, 0x86u, 0xE6u, 0x9Du, 0x8Bu,
    0xBDu, 0xCDu, 0xC7u, 0x80u, 0xB0u, 0x13u, 0xD3u, 0xECu,
    0x7Fu, 0xC0u, 0xE7u, 0x46u, 0xE9u, 0x58u, 0x92u, 0x2Cu,
    0xB7u, 0xC9u, 0x16u, 0x53u, 0x0Du, 0xD6u, 0x74u, 0x6Du,
    0x9Fu, 0x20u, 0x5Fu, 0xE2u, 0x8Cu, 0xDCu, 0x39u, 0x0Cu,
    0xDDu, 0x1Fu, 0xD1u, 0xB6u, 0x8Fu, 0x5Cu, 0x95u, 0xB8u,
    0x94u, 0x3Eu, 0x71u, 0x41u, 0x25u, 0x1Bu, 0x6Au, 0xA6u,
    0x03u, 0x0Eu, 0xCCu, 0x48u, 0x15u, 0x29u, 0x38u, 0x42u,
    0x1Cu, 0xC1u, 0x28u, 0xD9u, 0x19u, 0x36u, 0xB3u, 0x75u,
    0xEEu, 0x57u, 0xF0u, 0x9Bu, 0xB4u, 0xAAu, 0xF2u, 0xD4u,
    0xBFu, 0xA3u, 0x4Eu, 0xDAu, 0x89u, 0xC2u, 0xAFu, 0x6Eu,
    0x2Bu, 0x77u, 0xE0u, 0x47u, 0x7Au, 0x8Eu, 0x2Au, 0xA0u,
    0x68u, 0x30u, 0xF7u, 0x67u, 0x0Fu, 0x0Bu, 0x8Au, 0xEFu
};

/* CRC-16/CCITT-FALSE: polynomial 0x1021, initial value 0xFFFF, no final XOR. */
uint16_t crc16_feed(uint16_t crc, uint8_t byte)
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

uint8_t pearson_hash(const uint8_t *data, uint16_t len)
{
    uint8_t hash = 0u;

    while (len != 0u) {
        hash = pearson_table[(uint8_t)(hash ^ *data)];
        ++data;
        --len;
    }
    return hash;
}

/* The host duplicate tests supply their own prng draws. */
#ifndef HOST_DIGI_TEST
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
#endif
