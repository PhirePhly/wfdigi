#include "hardware.h"

#define FRAME_MAX 128u
#define AX25_UI 0x03u
#define AX25_PID 0xF0u
#define BEACON_JITTER 15u

static uint8_t frame[FRAME_MAX];

static bool call_blank(const uint8_t *call)
{
    uint8_t i;

    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        if (call[i] != ' ') {
            return false;
        }
    }
    return true;
}

static uint8_t quot_rem(uint8_t value, uint8_t *rem)
{
    uint8_t quot = 0u;

    while (value >= 10u) {
        value = (uint8_t)(value - 10u);
        ++quot;
    }
    *rem = value;
    return quot;
}

static void put_digits(uint8_t *dest, uint8_t value, uint8_t width)
{
    uint8_t i = width;

    while (i != 0u) {
        uint8_t rem;

        --i;
        value = quot_rem(value, &rem);
        dest[i] = (uint8_t)('0' + rem);
    }
}

static void encode_call(uint8_t *dest, const uint8_t *call, uint8_t ssid, uint8_t flags)
{
    uint8_t i;

    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        dest[i] = (uint8_t)(call[i] << 1);
    }
    dest[6] = (uint8_t)(0x60u | (uint8_t)((ssid & 0x0Fu) << 1) | flags);
}

static uint16_t minutes_to_seconds(uint8_t minutes)
{
    uint16_t span = minutes;

    /* 64*n - 4*n = 60*n, without a multiply helper. */
    return (uint16_t)((span << 6) - (span << 2));
}

uint16_t beacon_next_wait(void)
{
    uint16_t span;
    uint8_t jitter;

    if (g_config.beacon_every == 0u) {
        return 0u;
    }
    span = minutes_to_seconds(g_config.beacon_every);
    prng_stir(timer_seconds());
    jitter = (uint8_t)(prng_u8() & BEACON_JITTER);
    if (span > jitter) {
        span = (uint16_t)(span - jitter);
    }
    return span;
}

static void advance_path(void)
{
    uint8_t count = g_config.bpath_count;
    uint8_t next;

    if (count == 0u) {
        g_config.bpath_next = 0u;
        config_seal();
        return;
    }
    next = (uint8_t)(g_config.bpath_next + 1u);
    if (next >= count) {
        next = 0u;
    }
    g_config.bpath_next = next;
    config_seal();
}

bool beacon_send(void)
{
    uint16_t n = 0u;
    uint8_t slot = 0u;
    bool via = false;
    uint8_t i;
    const uint8_t *text;

    encode_call(&frame[n], tncid, 0u, 0x80u);
    n = (uint16_t)(n + 7u);
    if (g_config.bpath_count != 0u) {
        slot = g_config.bpath_next;
        if (slot >= g_config.bpath_count) {
            slot = 0u;
        }
        via = !call_blank(g_config.bpath[slot]);
    }
    encode_call(&frame[n], g_config.mycall, g_config.mycall_ssid, via ? 0u : 0x01u);
    n = (uint16_t)(n + 7u);
    if (via) {
        encode_call(&frame[n], g_config.bpath[slot], g_config.bpath_ssid[slot], 0x01u);
        n = (uint16_t)(n + 7u);
    }
    frame[n] = AX25_UI;
    ++n;
    frame[n] = AX25_PID;
    ++n;

    frame[n] = (uint8_t)'!';
    ++n;
    put_digits(&frame[n], g_config.loc_lat_deg, 2u);
    n = (uint16_t)(n + 2u);
    put_digits(&frame[n], g_config.loc_lat_min, 2u);
    n = (uint16_t)(n + 2u);
    frame[n] = (uint8_t)'.';
    ++n;
    put_digits(&frame[n], g_config.loc_lat_hund, 2u);
    n = (uint16_t)(n + 2u);
    frame[n] = g_config.loc_lat_ns;
    ++n;
    frame[n] = g_config.symbol_table;
    ++n;
    put_digits(&frame[n], g_config.loc_lon_deg, 3u);
    n = (uint16_t)(n + 3u);
    put_digits(&frame[n], g_config.loc_lon_min, 2u);
    n = (uint16_t)(n + 2u);
    frame[n] = (uint8_t)'.';
    ++n;
    put_digits(&frame[n], g_config.loc_lon_hund, 2u);
    n = (uint16_t)(n + 2u);
    frame[n] = g_config.loc_lon_ew;
    ++n;
    frame[n] = g_config.symbol_code;
    ++n;

    text = g_config.btext;
    for (i = 0u; i < BTEXT_LEN && text[i] != 0u; ++i) {
        if (n >= FRAME_MAX) {
            return false;
        }
        frame[n] = text[i];
        ++n;
    }
    if (n > FRAME_MAX || !modem_send(PKTQ_BEACON, frame, n)) {
        return false;
    }
    advance_path();
    return true;
}
