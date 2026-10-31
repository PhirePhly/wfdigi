#include "hardware.h"

#define FRAME_MAX 128u
#define BEACON_JITTER 15u

static uint8_t frame[FRAME_MAX];
static uint8_t bpath_next;
/* Pending quashed-path status, overwritten on each quash so only the last is
 * reported. Sent as the next beacon's comment text, then cleared.
 */
static uint8_t status_text[STATUS_MAX];
static bool status_pending;

void beacon_path_reset(void)
{
    bpath_next = 0u;
}

/* Record a quashed-path status to send with the next beacon. Overwrites any
 * pending status, so only the last quashed path is reported.
 */
void beacon_status_set(const uint8_t *text)
{
    uint8_t i = 0u;

    while (text[i] != 0u && i < (uint8_t)(STATUS_MAX - 1u)) {
        status_text[i] = text[i];
        ++i;
    }
    status_text[i] = 0u;
    status_pending = true;
}

/* Clear any pending quashed-path status and reset the beacon path cycle. The
 * SRAM is not zeroed at boot, so firmware_boot must call this before the first
 * beacon or a stale, non-zero status_pending would transmit a garbage status.
 */
void beacon_init(void)
{
    status_pending = false;
    status_text[0] = 0u;
    beacon_path_reset();
}

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
        bpath_next = 0u;
        return;
    }
    next = (uint8_t)(bpath_next + 1u);
    if (next >= count) {
        next = 0u;
    }
    bpath_next = next;
}

/* The beacon's current via path: the next cycle slot, and whether that slot is
 * a digipeater rather than a direct beacon.
 */
static void beacon_path(uint8_t *slot, bool *via)
{
    *slot = 0u;
    *via = false;
    if (g_config.bpath_count != 0u) {
        *slot = bpath_next;
        if (*slot >= g_config.bpath_count) {
            *slot = 0u;
        }
        *via = !call_blank(g_config.bpath[*slot]);
    }
}

/* Append the tncid>mycall[,bpath] UI header into frame; return the payload offset. */
static uint16_t begin_beacon(uint8_t slot, bool via)
{
    uint16_t n = 0u;

    encode_call(&frame[n], tncid, 0u, 0x80u);
    n = (uint16_t)(n + 7u);
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
    return n;
}

/* Append the position payload after a beacon header. Returns the total length,
 * or 0 when the text would not fit in the frame.
 */
static uint16_t append_position(uint16_t n)
{
    uint8_t i;
    const uint8_t *text;

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
            return 0u;
        }
        frame[n] = text[i];
        ++n;
    }
    if (n > FRAME_MAX) {
        return 0u;
    }
    return n;
}

/* Append the pending status text after a beacon header, then clear the pending
 * flag. status_text already begins with the '>' status indicator.
 */
static uint16_t append_status(uint16_t n)
{
    uint8_t k;

    for (k = 0u; status_text[k] != 0u && n < FRAME_MAX; ++k) {
        frame[n] = status_text[k];
        ++n;
    }
    status_pending = false;
    status_text[0] = 0u;
    return n;
}

/* Queue the position beacon and, if a status is pending, a second status
 * beacon on the same path slot, then advance to the next slot. The beacon
 * timer, BSEND, and ?APRSP all use this full beacon.
 */
bool beacon_send(void)
{
    uint8_t slot;
    bool via;
    uint16_t n;

    beacon_path(&slot, &via);
    n = append_position(begin_beacon(slot, via));
    if (n == 0u || !modem_send(frame, n, g_config.txstale * 2u)) {
        return false;
    }
    /* The position packet is already queued, so the frame buffer is free. */
    if (status_pending) {
        n = append_status(begin_beacon(slot, via));
        (void)modem_send(frame, n, g_config.txstale * 2u);
    }
    advance_path();
    return true;
}

/* ?APRSS: queue the pending status on the current path slot. It sends no
 * position packet, leaves the beacon countdown alone, and does not advance the
 * BPATH cycle. Returns false, sending nothing, when there is no status to
 * report.
 */
bool beacon_send_status(void)
{
    uint8_t slot;
    bool via;
    uint16_t n;

    if (!status_pending) {
        return false;
    }
    beacon_path(&slot, &via);
    n = append_status(begin_beacon(slot, via));
    (void)modem_send(frame, n, g_config.txstale * 2u);
    return true;
}
