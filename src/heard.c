#include "wfdigi.h"

/* Stations heard directly. Each entry holds a callsign and the second it was
 * most recently heard before any digipeater repeated it. A zero first
 * character marks an empty slot.
 */
typedef struct {
    uint8_t call[CALLSIGN_LEN];
    uint8_t ssid;
    uint16_t heard_at;
} HeardSlot;

static HeardSlot db[HEARD_SLOTS];

#define DB_END (db + HEARD_SLOTS)

static void clear_slot(HeardSlot *slot)
{
    uint8_t c;

    for (c = 0u; c < CALLSIGN_LEN; ++c) {
        slot->call[c] = 0u;
    }
    slot->ssid = 0u;
    slot->heard_at = 0u;
}

void heard_init(void)
{
    HeardSlot *slot = db;

    while (slot != DB_END) {
        clear_slot(slot);
        ++slot;
    }
}

static bool slot_match(const HeardSlot *slot, const uint8_t *call, uint8_t ssid)
{
    uint8_t i;

    if (slot->ssid != ssid) {
        return false;
    }
    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        if (slot->call[i] != call[i]) {
            return false;
        }
    }
    return true;
}

static HeardSlot *find_slot(const uint8_t *call, uint8_t ssid)
{
    HeardSlot *slot = db;

    while (slot != DB_END) {
        if (slot->call[0] != 0u && slot_match(slot, call, ssid)) {
            return slot;
        }
        ++slot;
    }
    return 0;
}

/* First empty slot. When every slot is in use, the oldest entry. */
static HeardSlot *available_slot(void)
{
    HeardSlot *slot = db;
    HeardSlot *oldest = db;
    uint16_t now = timer_seconds();
    uint16_t oldest_age = 0u;

    while (slot != DB_END) {
        uint16_t age;

        if (slot->call[0] == 0u) {
            return slot;
        }
        age = (uint16_t)(now - slot->heard_at);
        if (age >= oldest_age) {
            oldest_age = age;
            oldest = slot;
        }
        ++slot;
    }
    return oldest;
}

void heard_note(const uint8_t *ax_source)
{
    uint8_t call[CALLSIGN_LEN];
    uint8_t ssid;
    uint8_t i;
    HeardSlot *slot;

    ssid = (uint8_t)((ax_source[6] >> 1) & 0x0Fu);
    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        call[i] = letter_to_upper((uint8_t)(ax_source[i] >> 1));
    }
    if (call[0] == 0u) {
        return;
    }

    slot = find_slot(call, ssid);
    if (slot == 0) {
        slot = available_slot();
        for (i = 0u; i < CALLSIGN_LEN; ++i) {
            slot->call[i] = call[i];
        }
        slot->ssid = ssid;
    }
    slot->heard_at = timer_seconds();
}

void heard_sweep(void)
{
    uint16_t now = timer_seconds();
    HeardSlot *slot = db;

    while (slot != DB_END) {
        if (slot->call[0] != 0u && (uint16_t)(now - slot->heard_at) >= HEARD_MAX_AGE) {
            clear_slot(slot);
        }
        ++slot;
    }
}

/* CALL or CALL-SSID, at most nine characters. SSID 0 is omitted. */
static uint8_t format_call(const HeardSlot *slot, uint8_t *dest)
{
    uint8_t n = 0u;
    uint8_t i;
    uint8_t s = slot->ssid;

    for (i = 0u; i < CALLSIGN_LEN && slot->call[i] != (uint8_t)' '; ++i) {
        dest[n] = slot->call[i];
        ++n;
    }
    if (s != 0u) {
        dest[n] = (uint8_t)'-';
        ++n;
        if (s >= 10u) {
            dest[n] = (uint8_t)'1';
            ++n;
            s = (uint8_t)(s - 10u);
        }
        dest[n] = (uint8_t)('0' + s);
        ++n;
    }
    return n;
}

/* Occupied entries, in the order they sit in the table. *from is 0 to start
 * at the first slot, and otherwise the last slot already written.
 */
uint16_t heard_list(uint8_t *dest, uint16_t dest_max, const void **from)
{
    uint16_t now = timer_seconds();
    const HeardSlot *slot = *from == 0 ? db : (const HeardSlot *)*from + 1;
    const HeardSlot *prev = 0;
    uint16_t n = 0u;
    uint8_t count = 0u;

    while (slot != DB_END && count < HEARD_PAGE) {
        uint8_t text[CALLSIGN_LEN + 3u];
        uint8_t len;
        uint8_t i;

        if (slot->call[0] == 0u || (uint16_t)(now - slot->heard_at) >= HEARD_MAX_AGE) {
            ++slot;
            continue;
        }
        len = format_call(slot, text);
        if ((uint16_t)(n + (n != 0u ? 1u : 0u) + len) > dest_max) {
            /* The next call does not fit. Resume at the last one written.
             * A call that does not fit in an empty page cannot be sent.
             */
            *from = n == 0u ? 0 : (const void *)prev;
            return n;
        }
        if (n != 0u) {
            dest[n] = (uint8_t)' ';
            ++n;
        }
        for (i = 0u; i < len; ++i) {
            dest[n] = text[i];
            ++n;
        }
        prev = slot;
        ++count;
        ++slot;
    }
    // Scan the rest of the table to check if we're done
    while (slot != DB_END) {
        if (slot->call[0] != 0u && (uint16_t)(now - slot->heard_at) < HEARD_MAX_AGE) {
            *from = (const void *)prev;
            return n;
        }
        ++slot;
    }
    *from = 0;
    return n;
}
