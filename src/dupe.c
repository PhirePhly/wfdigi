#include "wfdigi.h"

/* Recently sent packets. The scan compares the information-field sum first. */
#define AX25_ADDR 7u
#define AX25_MAX_ADDRS 10u

typedef struct {
    uint8_t call[CALLSIGN_LEN];
    uint8_t ssid;
    uint8_t sum;
    uint16_t sent;
} DupeSlot;

static DupeSlot db[DUPE_SLOTS];
static DupeSlot *db_end;

/* Low 8 bits of the sum of the printable information-field bytes. Zero marks an empty slot. */
static uint8_t info_sum(const uint8_t *info, uint16_t len)
{
    uint8_t sum = 0u;

    while (len != 0u) {
        uint8_t c = *info;

        /* Skip padding such as a trailing CR so two copies of the same text match. */
        if (c >= 0x20u && c <= 0x7Eu) {
            sum = (uint8_t)(sum + c);
        }
        ++info;
        --len;
    }
    if (sum == 0u) {
        return 1u;
    }
    return sum;
}

static void clear_slot(DupeSlot *slot)
{
    uint8_t c;

    for (c = 0u; c < CALLSIGN_LEN; ++c) {
        slot->call[c] = 0u;
    }
    slot->ssid = 0u;
    slot->sum = 0u;
    slot->sent = 0u;
}

static bool call_same(const uint8_t *a, const uint8_t *b)
{
    uint8_t i;

    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

/* Source address, and the information field after control and PID. */
static bool frame_parts(const uint8_t *frame, uint16_t len, const uint8_t **src, uint8_t *ssid,
                        const uint8_t **info, uint16_t *info_len)
{
    uint16_t at = 0u;
    uint8_t n = 0u;
    uint8_t ctl;

    if (frame == 0 || len < (uint16_t)(AX25_ADDR + AX25_ADDR)) {
        return false;
    }
    while (n < AX25_MAX_ADDRS && (uint16_t)(at + AX25_ADDR) <= len) {
        uint8_t ext = (uint8_t)(frame[(uint16_t)(at + 6u)] & 0x01u);

        at = (uint16_t)(at + AX25_ADDR);
        ++n;
        if (ext != 0u) {
            break;
        }
    }
    if (n < 2u || (frame[(uint16_t)(at - 1u)] & 0x01u) == 0u) {
        return false;
    }
    *src = frame + AX25_ADDR;
    *ssid = (uint8_t)((frame[(uint16_t)(AX25_ADDR + AX25_ADDR - 1u)] >> 1) & 0x0Fu);
    if (at >= len) {
        *info = frame + at;
        *info_len = 0u;
        return true;
    }
    ctl = frame[at];
    at = (uint16_t)(at + 1u);
    /* I frames and UI frames carry a PID byte ahead of the information field. */
    if (((ctl & 0x01u) == 0u) || ((ctl & 0xEFu) == 0x03u)) {
        if (at < len) {
            at = (uint16_t)(at + 1u);
        }
    }
    *info = frame + at;
    *info_len = (uint16_t)(len - at);
    return true;
}

void dupe_init(void)
{
    DupeSlot *p = db;
    uint8_t n = 0u;

    do {
        clear_slot(p);
        ++p;
        ++n;
    } while (n < DUPE_SLOTS);
    db_end = p;
}

/* First empty slot. When every entry is still inside the window, the oldest one. */
static DupeSlot *available_slot(void)
{
    DupeSlot *slot = db;
    DupeSlot *oldest = db;
    uint16_t now = timer_seconds();
    uint16_t oldest_age = 0u;

    while (slot != db_end) {
        uint16_t age;

        if (slot->sum == 0u) {
            return slot;
        }
        age = (uint16_t)(now - slot->sent);
        if (age >= oldest_age) {
            oldest_age = age;
            oldest = slot;
        }
        ++slot;
    }
    return oldest;
}

void dupe_remember(const uint8_t *frame, uint16_t len)
{
    const uint8_t *src;
    const uint8_t *info;
    uint16_t info_len;
    uint8_t ssid;
    uint8_t i;
    DupeSlot *slot;

    if (!frame_parts(frame, len, &src, &ssid, &info, &info_len)) {
        return;
    }
    slot = available_slot();
    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        slot->call[i] = src[i];
    }
    slot->ssid = ssid;
    slot->sum = info_sum(info, info_len);
    slot->sent = timer_seconds();
}

bool dupe_recent(const uint8_t *frame, uint16_t len)
{
    const uint8_t *src;
    const uint8_t *info;
    uint16_t info_len;
    uint8_t ssid;
    uint8_t sum;
    DupeSlot *slot;
    bool found = false;

    if (!frame_parts(frame, len, &src, &ssid, &info, &info_len)) {
        return false;
    }
    sum = info_sum(info, info_len);
    slot = db;
    while (slot != db_end) {
        if (slot->sum != 0u) {
            if (!timer_in_dupe_window(slot->sent)) {
                clear_slot(slot);
            } else if (slot->sum == sum && call_same(slot->call, src) && slot->ssid == ssid) {
                found = true;
            }
        }
        ++slot;
    }
    return found;
}
