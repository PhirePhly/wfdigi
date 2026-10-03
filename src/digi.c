#include "wfdigi.h"

/* Received frames are at most this long. The transmit queue uses the same limit. */
#define FRAME_MAX 330u
#define AX25_ADDR 7u
#define AX25_MAX_VIAS 8u
#define NOT_FOUND 0xFFu
#define AX25_H 0x80u
#define AX25_EXT 0x01u
#define AX25_RESERVED 0x60u

static uint8_t work[FRAME_MAX];
static uint16_t work_len;

static bool call_eq(const uint8_t *ax, const uint8_t *call, uint8_t ssid)
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

static uint8_t *via_ptr(uint8_t index)
{
    uint8_t *p = work + (AX25_ADDR + AX25_ADDR);

    while (index != 0u) {
        p += AX25_ADDR;
        --index;
    }
    return p;
}

static bool load_frame(const uint8_t *frame, uint16_t len, uint8_t *vias)
{
    uint16_t i;
    uint16_t at = 0u;
    uint8_t n = 0u;

    if (frame == 0 || len < (AX25_ADDR + AX25_ADDR + 1u) || len > FRAME_MAX) {
        return false;
    }
    for (i = 0u; i < len; ++i) {
        work[i] = frame[i];
    }
    work_len = len;
    while (n < (uint8_t)(2u + AX25_MAX_VIAS) && (uint16_t)(at + AX25_ADDR) <= len) {
        uint8_t ext = (uint8_t)(work[(uint16_t)(at + 6u)] & AX25_EXT);

        at = (uint16_t)(at + AX25_ADDR);
        ++n;
        if (ext != 0u) {
            if (n < 2u) {
                return false;
            }
            *vias = (uint8_t)(n - 2u);
            return true;
        }
    }
    return false;
}

static void write_mycall(uint8_t *dest, uint8_t flags)
{
    uint8_t i;

    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        dest[i] = (uint8_t)(g_config.mycall[i] << 1);
    }
    dest[6] = (uint8_t)(AX25_RESERVED | (uint8_t)((g_config.mycall_ssid & 0x0Fu) << 1) | flags);
}

static void mark_before(uint8_t index)
{
    uint8_t i;
    uint8_t *p = via_ptr(0u);

    for (i = 0u; i < index; ++i) {
        p[6] = (uint8_t)(p[6] | AX25_H);
        p += AX25_ADDR;
    }
}

static void mark_all(uint8_t vias)
{
    uint8_t i;
    uint8_t *p = via_ptr(0u);

    for (i = 0u; i < vias; ++i) {
        p[6] = (uint8_t)(p[6] | AX25_H);
        p += AX25_ADDR;
    }
}

/* Add one digipeater after the current path and move the payload up. */
static bool append_via(uint8_t vias, const uint8_t call_ax[CALLSIGN_LEN], uint8_t ssid, uint8_t flags)
{
    uint16_t start;
    uint16_t at;
    uint8_t *last;
    uint8_t i;

    if (vias == 0u || vias >= AX25_MAX_VIAS) {
        return false;
    }
    if (work_len > (uint16_t)(FRAME_MAX - AX25_ADDR)) {
        return false;
    }
    last = via_ptr((uint8_t)(vias - 1u));
    last[6] = (uint8_t)(last[6] & (uint8_t)~AX25_EXT);
    start = (uint16_t)(AX25_ADDR + AX25_ADDR);
    for (i = 0u; i < vias; ++i) {
        start = (uint16_t)(start + AX25_ADDR);
    }
    at = work_len;
    while (at > start) {
        --at;
        work[(uint16_t)(at + AX25_ADDR)] = work[at];
    }
    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        work[(uint16_t)(start + i)] = call_ax[i];
    }
    work[(uint16_t)(start + 6u)] =
        (uint8_t)(AX25_RESERVED | (uint8_t)((ssid & 0x0Fu) << 1) | flags);
    work_len = (uint16_t)(work_len + AX25_ADDR);
    return true;
}

static bool append_mycall(uint8_t vias)
{
    uint8_t call_ax[CALLSIGN_LEN];
    uint8_t i;

    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        call_ax[i] = (uint8_t)(g_config.mycall[i] << 1);
    }
    return append_via(vias, call_ax, g_config.mycall_ssid, (uint8_t)(AX25_H | AX25_EXT));
}

static void send_work(void)
{
    /* Record before the modem finishes sending, so a second copy is caught. */
    dupe_remember(work, work_len);
    (void)modem_send(PKTQ_AX25, work, work_len);
}

/* First matching via. unused_only skips addresses that already have the H bit. */
static uint8_t find_call(uint8_t vias, const uint8_t *call, uint8_t ssid, bool unused_only)
{
    uint8_t i;
    uint8_t *p = via_ptr(0u);

    for (i = 0u; i < vias; ++i) {
        if (!unused_only || (p[6] & AX25_H) == 0u) {
            if (call_eq(p, call, ssid)) {
                return i;
            }
        }
        p += AX25_ADDR;
    }
    return NOT_FOUND;
}

static uint8_t find_alias(uint8_t vias)
{
    uint8_t i;
    uint8_t *p = via_ptr(0u);

    for (i = 0u; i < vias; ++i) {
        if ((p[6] & AX25_H) == 0u) {
            uint8_t a;

            for (a = 0u; a < ALIAS_COUNT; ++a) {
                if (g_config.alias[a][0] != (uint8_t)' ' &&
                    call_eq(p, g_config.alias[a], g_config.alias_ssid[a])) {
                    return i;
                }
            }
        }
        p += AX25_ADDR;
    }
    return NOT_FOUND;
}

/* Prefix plus digit N, SSID n, with 1 <= n <= N. The H bit is ignored here. */
static bool nn_match(const uint8_t *ax, const uint8_t *prefix, uint8_t *n, uint8_t *limit)
{
    uint8_t len = 0u;
    uint8_t i;
    uint8_t digit;
    uint8_t ssid;

    while (len < NNALIAS_LEN && prefix[len] != (uint8_t)' ') {
        ++len;
    }
    if (len == 0u) {
        return false;
    }
    for (i = 0u; i < len; ++i) {
        if (ax[i] != (uint8_t)(prefix[i] << 1)) {
            return false;
        }
    }
    digit = (uint8_t)((ax[len] >> 1) & 0x7Fu);
    if (digit < (uint8_t)'1' || digit > (uint8_t)'7') {
        return false;
    }
    for (i = (uint8_t)(len + 1u); i < CALLSIGN_LEN; ++i) {
        if (ax[i] != (uint8_t)((uint8_t)' ' << 1)) {
            return false;
        }
    }
    ssid = (uint8_t)((ax[6] >> 1) & 0x0Fu);
    *limit = (uint8_t)(digit - (uint8_t)'0');
    *n = ssid;
    return ssid >= 1u && ssid <= *limit;
}

static bool nn_match_any(const uint8_t *ax, uint8_t *n, uint8_t *limit)
{
    uint8_t a;

    for (a = 0u; a < NNALIAS_COUNT; ++a) {
        if (nn_match(ax, g_config.nnalias[a], n, limit)) {
            return true;
        }
    }
    return false;
}

static uint8_t find_nn(uint8_t vias, uint8_t *remain)
{
    uint8_t i;
    uint8_t *p = via_ptr(0u);

    for (i = 0u; i < vias; ++i) {
        uint8_t n;
        uint8_t limit;

        if ((p[6] & AX25_H) == 0u && nn_match_any(p, &n, &limit)) {
            *remain = n;
            return i;
        }
        p += AX25_ADDR;
    }
    return NOT_FOUND;
}

/* DIRECTONLY: no H bit yet, and every configured n-N via is still n == N. */
static bool path_is_fresh(uint8_t vias)
{
    uint8_t i;
    uint8_t *p = via_ptr(0u);

    for (i = 0u; i < vias; ++i) {
        uint8_t n;
        uint8_t limit;

        if ((p[6] & AX25_H) != 0u) {
            return false;
        }
        if (nn_match_any(p, &n, &limit) && n != limit) {
            return false;
        }
        p += AX25_ADDR;
    }
    return true;
}

/* An unused n-N address counts as its remaining SSID. Any other unused address counts as one. */
static uint8_t requested_hops(const uint8_t *ax)
{
    uint8_t i;
    uint8_t digit = 0u;
    uint8_t ssid;
    bool saw = false;

    ssid = (uint8_t)((ax[6] >> 1) & 0x0Fu);
    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        uint8_t c = (uint8_t)((ax[i] >> 1) & 0x7Fu);

        if (c != (uint8_t)' ') {
            saw = true;
            digit = c;
        }
    }
    if (saw && digit >= (uint8_t)'1' && digit <= (uint8_t)'7') {
        uint8_t limit = (uint8_t)(digit - (uint8_t)'0');

        if (ssid >= 1u && ssid <= limit) {
            return ssid;
        }
    }
    return 1u;
}

static uint8_t used_hops(uint8_t vias)
{
    uint8_t total = 0u;
    uint8_t i;
    uint8_t *p = via_ptr(0u);

    for (i = 0u; i < vias; ++i) {
        if ((p[6] & AX25_H) != 0u && total != 255u) {
            ++total;
        }
        p += AX25_ADDR;
    }
    return total;
}

static uint8_t pending_hops(uint8_t vias)
{
    uint8_t total = 0u;
    uint8_t i;
    uint8_t *p = via_ptr(0u);

    for (i = 0u; i < vias; ++i) {
        uint8_t add;

        if ((p[6] & AX25_H) == 0u) {
            add = requested_hops(p);
            if ((uint8_t)(255u - total) < add) {
                return 255u;
            }
            total = (uint8_t)(total + add);
        }
        p += AX25_ADDR;
    }
    return total;
}

static void rewrite_alias(uint8_t index)
{
    uint8_t *p = via_ptr(index);
    uint8_t ext = (uint8_t)(p[6] & AX25_EXT);

    mark_before(index);
    write_mycall(p, (uint8_t)(AX25_H | ext));
}

static void rewrite_nn(uint8_t index, uint8_t vias, uint8_t remain)
{
    uint8_t *p = via_ptr(index);
    uint8_t saved[CALLSIGN_LEN];
    uint8_t i;

    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        saved[i] = p[i];
    }
    mark_before(index);
    if (remain <= 1u) {
        uint8_t ext = (uint8_t)(p[6] & AX25_EXT);

        write_mycall(p, (uint8_t)(AX25_H | ext));
        send_work();
        return;
    }
    /* The reduced n-N is appended, so this address is no longer the end. */
    write_mycall(p, AX25_H);
    if (append_via(vias, saved, (uint8_t)(remain - 1u), AX25_EXT)) {
        send_work();
    }
}

void digi_ingress(const uint8_t *frame, uint16_t len)
{
    uint8_t vias;
    uint8_t mine;
    uint8_t alias_at;
    uint8_t nn_at;
    uint8_t remain;

    if (g_config.digipeat == 0u) {
        return;
    }
    if (!load_frame(frame, len, &vias) || vias == 0u) {
        return;
    }
    /* Hearing our own source would repeat a beacon we just sent. */
    if (call_eq(work + AX25_ADDR, g_config.mycall, g_config.mycall_ssid)) {
        return;
    }
    if (dupe_recent(work, work_len)) {
        return;
    }
    /* MAXHOPS already-used digipeaters end the trip. Do not add another. */
    if (used_hops(vias) >= g_config.maxhops) {
        return;
    }
    mine = find_call(vias, g_config.mycall, g_config.mycall_ssid, true);
    if (mine != NOT_FOUND) {
        uint8_t *mine_at = via_ptr(mine);

        /* Preempt: every hop through our callsign has now been repeated. */
        mark_before(mine);
        mine_at[6] = (uint8_t)(mine_at[6] | AX25_H);
        send_work();
        return;
    }
    if (find_call(vias, g_config.mycall, g_config.mycall_ssid, false) != NOT_FOUND) {
        return;
    }
    alias_at = find_alias(vias);
    nn_at = NOT_FOUND;
    remain = 0u;
    if (alias_at == NOT_FOUND) {
        nn_at = find_nn(vias, &remain);
        if (nn_at == NOT_FOUND) {
            return;
        }
    }
    if (g_config.directonly != 0u && !path_is_fresh(vias)) {
        return;
    }
    /* A request equal to MAXHOPS is repeated normally. Only a larger request is killed. */
    if (pending_hops(vias) > g_config.maxhops) {
        mark_all(vias);
        if (append_mycall(vias)) {
            send_work();
        }
        return;
    }
    if (alias_at != NOT_FOUND) {
        rewrite_alias(alias_at);
        send_work();
        return;
    }
    rewrite_nn(nn_at, vias, remain);
}
