#include "wfdigi.h"

#define NOT_FOUND 0xFFu

static uint8_t work[AX25_MAX];
static uint16_t work_len;

/* Uniform draw in [low, high]. The limits are 1..9, so repeated subtraction
 * avoids pulling a division helper into the firmware.
 */
static uint8_t random_seconds(uint8_t low, uint8_t high)
{
    uint8_t span = (uint8_t)(high - low + 1u);
    uint16_t limit = span;
    uint8_t draw;

    while ((uint16_t)(limit + span) <= 256u) {
        limit = (uint16_t)(limit + span);
    }
    prng_stir(timer_seconds());
    do {
        draw = prng_u8();
    } while ((uint16_t)draw >= limit);
    while (draw >= span) {
        draw = (uint8_t)(draw - span);
    }
    return (uint8_t)(low + draw);
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

/* Copy the modem buffer, then let the receiver reuse it. Parsing uses work. */
static bool take_frame(const uint8_t *frame, uint16_t len)
{
    uint16_t i;

    if (frame == 0 || len < (AX25_ADDR + AX25_ADDR + 1u) || len > AX25_MAX) {
        return false;
    }
    for (i = 0u; i < len; ++i) {
        work[i] = frame[i];
    }
    work_len = len;
    modem_rx_release();
    return true;
}

static bool count_vias(uint8_t *vias)
{
    uint16_t at = 0u;
    uint8_t n = 0u;
    uint16_t len = work_len;

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
    /* Eight digipeaters is the limit. No end bit after that many is a format drop. */
    if (n >= (uint8_t)(2u + AX25_MAX_VIAS)) {
        note_drop_f();
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
    if (work_len > (uint16_t)(AX25_MAX - AX25_ADDR)) {
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

static void send_work_now(void)
{
    /* Record before the modem finishes sending, so a second copy is caught. */
    dupe_remember(work, work_len);
    (void)modem_send(work, work_len, g_config.txstale);
}

static void send_work(void)
{
    if (g_config.viscous_min != 0u) {
        uint8_t delay = random_seconds(g_config.viscous_min, g_config.viscous_max);

        (void)modem_queue_viscous(work, work_len, pktq_expire_in(delay));
        return;
    }
    send_work_now();
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

/* This station already repeated the frame, so another copy would be a loop. */
static bool mycall_used(uint8_t vias)
{
    uint8_t i;
    uint8_t *p = via_ptr(0u);

    for (i = 0u; i < vias; ++i) {
        if ((p[6] & AX25_H) != 0u && call_eq(p, g_config.mycall, g_config.mycall_ssid)) {
            return true;
        }
        p += AX25_ADDR;
    }
    return false;
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

/* Prefix plus digit N (1-7). *limit is N and *n is the SSID. The H bit is ignored. */
static bool nn_shape(const uint8_t *ax, const uint8_t *prefix, uint8_t *n, uint8_t *limit)
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
    return true;
}

/* 1 <= n <= N. An SSID above N is a different path and is quashed instead. */
static bool nn_match(const uint8_t *ax, const uint8_t *prefix, uint8_t *n, uint8_t *limit)
{
    return nn_shape(ax, prefix, n, limit) && *n >= 1u && *n <= *limit;
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

/* Unused n-N whose SSID is above N, such as WIDE1-2. That is not a hop request. */
static bool path_nn_invalid(uint8_t vias)
{
    uint8_t i;
    uint8_t *p = via_ptr(0u);

    for (i = 0u; i < vias; ++i) {
        if ((p[6] & AX25_H) == 0u) {
            uint8_t a;

            for (a = 0u; a < NNALIAS_COUNT; ++a) {
                uint8_t n;
                uint8_t limit;

                if (nn_shape(p, g_config.nnalias[a], &n, &limit) && n > limit) {
                    return true;
                }
            }
        }
        p += AX25_ADDR;
    }
    return false;
}

/* DIRECTONLY: the first digipeater has not been repeated yet. A later
 * WIDE2-1 is still direct when the sender wrote it that way.
 */
static bool path_is_fresh(uint8_t vias)
{
    if (vias == 0u) {
        return false;
    }
    return (via_ptr(0u)[6] & AX25_H) == 0u;
}

/* A configured n-N address counts as its remaining SSID. Anything else counts as one. */
static uint8_t requested_hops(const uint8_t *ax)
{
    uint8_t n;
    uint8_t limit;

    if (nn_match_any(ax, &n, &limit)) {
        return n;
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

/* Take one hop from the n-N we matched. SSID 1 stays and the quash marks it used. */
static void reduce_nn(uint8_t index, uint8_t remain)
{
    uint8_t *p;
    uint8_t ssid_mask;

    if (remain <= 1u) {
        return;
    }
    p = via_ptr(index);
    ssid_mask = (uint8_t)(0x0Fu << 1);
    p[6] = (uint8_t)((p[6] & (uint8_t)~ssid_mask) | (uint8_t)((uint8_t)(remain - 1u) << 1));
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
        // We're able to delete the depleted n-N alias
        uint8_t ext = (uint8_t)(p[6] & AX25_EXT);

        write_mycall(p, (uint8_t)(AX25_H | ext));
        send_work();
        return;
    }
    /* The reduced n-N is appended, so this address is no longer the end. */
    write_mycall(p, AX25_H);
    if (append_via(vias, saved, (uint8_t)(remain - 1u), AX25_EXT)) {
        send_work();
    } else {
        // We're unable to append the reduced n-N alias, so we drop it
        note_drop_f();
    }
}

// Returns true if the frame was successfully ingested and processed.
bool digi_ingress(const uint8_t *frame, uint16_t len)
{
    uint8_t vias;
    uint8_t mine;
    uint8_t alias_at;
    uint8_t nn_at;
    uint8_t remain;
    uint8_t used;
    uint8_t ahead;
    bool direct;
    bool nn_invalid;

    if (!take_frame(frame, len)) {
        // Failed to take the frame, so it is not ingested.
        return false;
    }
    logging_frame('R', work, work_len);
    // Process frame for messaging / queries
    if (process_frame_for_messaging(work, work_len)) {
        // If we respond to a message, we don't need to digipeat it
        return true;
    }
    if (g_config.digipeat == 0u) {
        // Digipeating is disabled.
        return true;
    }
    if (!count_vias(&vias) || vias == 0u) {
        // We failed to correct a reasonable number of vias
        // or the packet has no vias and is direct only and doesn't need digipeating
        return true;
    }
    if (call_eq(work + AX25_ADDR, g_config.mycall, g_config.mycall_ssid)) {
        // Hearing our own source would repeat a beacon we just sent.
        return true;
    }
    if (mycall_used(vias)) {
        // Our callsign has been used in the path already.
        return true;
    }
    if (dupe_recent(work, work_len)) {
        // We have already sent this frame recently.
        return true;
    }
    direct = path_is_fresh(vias);
    mine = find_call(vias, g_config.mycall, g_config.mycall_ssid, true);
    if (mine == NOT_FOUND && g_config.viscous_min != 0u && !direct) {
        // This is kind of a hack where we pretend we transmit all non-direct packets
        // when in Viscous mode so packets digipeated by other stations "count"
        // as us already sending them and this suppresses packets in the Viscous queue.
        dupe_remember(work, work_len);
        return true;
    }

    used = used_hops(vias);
    if (used >= g_config.maxhops) {
        // Already used at least MAXHOPS digipeaters, so we silently drop it
        return true;
    }

    if (mine != NOT_FOUND) {
        uint8_t *mine_at = via_ptr(mine);

        /* Preempt: every hop through our callsign has now been repeated. */
        mark_before(mine);
        mine_at[6] = (uint8_t)(mine_at[6] | AX25_H);
        /* A specific request for this station is never held by VISCOUS. */
        send_work_now();
        return true;
    }
    // Look for any of our aliases or n-N aliases
    alias_at = find_alias(vias);
    nn_at = NOT_FOUND;
    remain = 0u;
    nn_invalid = path_nn_invalid(vias);
    if (alias_at == NOT_FOUND) {
        nn_at = find_nn(vias, &remain);
        if (nn_at == NOT_FOUND && !nn_invalid) {
            // No aliases or n-N aliases found, so we silently drop it
            return true;
        }
    }
    if (g_config.directonly != 0u && !direct) {
        // Non-direct packet when we are direct only
        return true;
    }
    // How many unused hops are still in the path?
    ahead = pending_hops(vias);
    if (nn_invalid || (uint8_t)(255u - used) < ahead || (uint8_t)(used + ahead) > g_config.maxhops) {
        /* An SSID above N is already invalid, so leave every hop count alone.
         * A request past MAXHOPS still gives up one hop before the quash.
         */
        if (!nn_invalid && nn_at != NOT_FOUND) {
            reduce_nn(nn_at, remain);
        }
        mark_all(vias);
        if (append_mycall(vias)) {
            send_work();
        } else if (vias >= AX25_MAX_VIAS) {
            /* The path already holds eight digipeaters. Quash it by stamping MYCALL over the last one. */
            write_mycall(via_ptr((uint8_t)(vias - 1u)), (uint8_t)(AX25_H | AX25_EXT));
            send_work();
        } else {
            /* The quashed path needs one more address and the frame cannot grow. */
            note_drop_f();
        }
        return true;
    }
    if (alias_at != NOT_FOUND) {
        rewrite_alias(alias_at);
        send_work();
        return true;
    }
    rewrite_nn(nn_at, vias, remain);
    return true;
}

void digi_service(void)
{
    uint16_t expire;

    /* One due frame per call. The next call takes the one behind it. */
    if (!pktq_peek_expire(PKTQ_VISCOUS, &expire) || !pktq_expired(expire)) {
        return;
    }
    if (!pktq_take(PKTQ_VISCOUS, work, AX25_MAX, &work_len, &expire)) {
        return;
    }
    if (!dupe_recent(work, work_len)) {
        dupe_remember(work, work_len);
        /* txstale == 0 disables the stale timeout entirely. Otherwise the
         * frame expires txstale - viscous_min seconds from now, clamped to
         * at least 1 so a frame already past its viscous delay goes stale
         * on the next service pass.
         */
        uint16_t expire;
        if (g_config.txstale == 0u) {
            expire = PKTQ_EXPIRE_SPAN;
        } else if (g_config.txstale > g_config.viscous_min) {
            expire = (uint16_t)(g_config.txstale - g_config.viscous_min);
        } else {
            expire = 1u;
        }
        (void)modem_send(work, work_len, expire);
    } else {
        telemetry_note_viscous();
        logging_frame('V', work, work_len);
    }
}
