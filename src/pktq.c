#include "wfdigi.h"

/* 32-byte blocks: one link byte and 31 bytes of frame data. Index 0xFF is
 * the end of a chain and also means the free list is empty. 255 blocks let
 * short frames share the pool instead of each reserving a full MTU.
 * Transmit and viscous each have a 16-deep ring. Handles sit in expiry
 * order, soonest first. Each slot stores the second when it expires.
 * The caller that takes a frame decides what a reached expiry means.
 */
#define BLOCKS 255u
#define BLOCK_BYTES 32u
#define BLOCK_DATA 31u
#define BLOCK_END 0xFFu
#define QUEUE_MAX 16u
#define FRAME_MAX 330u

static uint8_t block[BLOCKS][BLOCK_BYTES];
static uint8_t free_head;

static uint8_t q_head[PKTQ_COUNT];
static uint8_t q_tail[PKTQ_COUNT];
static uint8_t q_count[PKTQ_COUNT];
static uint8_t slot_head[PKTQ_COUNT][QUEUE_MAX];
static uint8_t slot_tail[PKTQ_COUNT][QUEUE_MAX];
static uint16_t slot_len[PKTQ_COUNT][QUEUE_MAX];
static uint16_t slot_expire[PKTQ_COUNT][QUEUE_MAX];

static bool queue_ok(uint8_t queue)
{
    return queue < PKTQ_COUNT;
}

bool pktq_expired(uint16_t expire)
{
    /* A forward span of 1..32768 seconds is still waiting. 0 and a past
     * deadline inside that same window are due.
     */
    return (uint16_t)(timer_seconds() - expire) < 0x8000u;
}

static void free_chain(uint8_t head)
{
    uint8_t guard = 0u;

    while (head != BLOCK_END && guard < BLOCKS) {
        uint8_t next = block[head][0];

        block[head][0] = free_head;
        free_head = head;
        head = next;
        ++guard;
    }
}

static bool alloc_block(uint8_t *id)
{
    uint8_t got;

    if (free_head == BLOCK_END) {
        return false;
    }
    got = free_head;
    free_head = block[got][0];
    block[got][0] = BLOCK_END;
    *id = got;
    return true;
}

/* True when a is due before b. Equal seconds are not before each other. */
static bool expire_before(uint16_t a, uint16_t b)
{
    return a != b && (uint16_t)(b - a) < 0x8000u;
}

static void place_slot(uint8_t queue, uint8_t at, uint8_t head, uint8_t tail,
                        uint16_t len, uint16_t expire)
{
    uint8_t dest;
    uint8_t src;

    /* The usual packet expires with or after the last one, so it lands on
     * the tail and nothing else moves.
     */
    if (at != q_tail[queue]) {
        dest = q_tail[queue];
        src = (uint8_t)((q_tail[queue] - 1u) & (QUEUE_MAX - 1u));
        for (;;) {
            slot_head[queue][dest] = slot_head[queue][src];
            slot_tail[queue][dest] = slot_tail[queue][src];
            slot_len[queue][dest] = slot_len[queue][src];
            slot_expire[queue][dest] = slot_expire[queue][src];
            if (src == at) {
                break;
            }
            dest = src;
            src = (uint8_t)((src - 1u) & (QUEUE_MAX - 1u));
        }
    }
    slot_head[queue][at] = head;
    slot_tail[queue][at] = tail;
    slot_len[queue][at] = len;
    slot_expire[queue][at] = expire;
    q_tail[queue] = (uint8_t)((q_tail[queue] + 1u) & (QUEUE_MAX - 1u));
    ++q_count[queue];
}

/* Index of the first handle due after expire, or the tail when this frame
 * belongs at the end.
 */
static uint8_t insert_at(uint8_t queue, uint16_t expire)
{
    uint8_t at;
    uint8_t last;
    uint8_t guard;

    if (q_count[queue] == 0u) {
        return q_tail[queue];
    }
    last = (uint8_t)((q_tail[queue] - 1u) & (QUEUE_MAX - 1u));
    if (!expire_before(expire, slot_expire[queue][last])) {
        return q_tail[queue];
    }
    at = q_head[queue];
    guard = 0u;
    while (guard < q_count[queue] && !expire_before(expire, slot_expire[queue][at])) {
        at = (uint8_t)((at + 1u) & (QUEUE_MAX - 1u));
        ++guard;
    }
    return at;
}

static void drop_head(uint8_t queue)
{
    free_chain(slot_head[queue][q_head[queue]]);
    q_head[queue] = (uint8_t)((q_head[queue] + 1u) & (QUEUE_MAX - 1u));
    --q_count[queue];
}

uint16_t pktq_expire_in(uint16_t seconds)
{
    if (seconds > PKTQ_EXPIRE_SPAN) {
        seconds = PKTQ_EXPIRE_SPAN;
    }
    return (uint16_t)(timer_seconds() + seconds);
}

void pktq_init(void)
{
    uint8_t i;
    uint8_t queue;

    for (i = 0u; i < (uint8_t)(BLOCKS - 1u); ++i) {
        block[i][0] = (uint8_t)(i + 1u);
    }
    block[BLOCKS - 1u][0] = BLOCK_END;
    free_head = 0u;
    for (queue = 0u; queue < PKTQ_COUNT; ++queue) {
        q_head[queue] = 0u;
        q_tail[queue] = 0u;
        q_count[queue] = 0u;
    }
}

bool pktq_pending(uint8_t queue)
{
    if (!queue_ok(queue)) {
        return false;
    }
    return q_count[queue] != 0u;
}

bool pktq_put(uint8_t queue, const uint8_t *data, uint16_t len, uint16_t expire)
{
    uint8_t head = BLOCK_END;
    uint8_t tail = BLOCK_END;
    uint8_t slot;
    uint16_t at = 0u;

    if (!queue_ok(queue) || data == 0 || len == 0u || len > FRAME_MAX) {
        return false;
    }
    if (q_count[queue] >= QUEUE_MAX) {
        return false;
    }
    while (at < len) {
        uint8_t id;
        uint8_t n;
        uint8_t i;
        uint16_t remain;

        if (!alloc_block(&id)) {
            free_chain(head);
            return false;
        }
        if (head == BLOCK_END) {
            head = id;
        } else {
            block[tail][0] = id;
        }
        tail = id;
        remain = (uint16_t)(len - at);
        n = BLOCK_DATA;
        if (remain < BLOCK_DATA) {
            n = (uint8_t)remain;
        }
        for (i = 0u; i < n; ++i) {
            block[id][(uint8_t)(1u + i)] = data[at + i];
        }
        at = (uint16_t)(at + n);
    }
    slot = insert_at(queue, expire);
    place_slot(queue, slot, head, tail, len, expire);
    return true;
}

bool pktq_take(uint8_t queue, uint8_t *dest, uint16_t dest_max, uint16_t *len, uint16_t *expire)
{
    uint8_t head;
    uint16_t n;
    uint16_t at;
    uint8_t guard;

    if (!queue_ok(queue) || dest == 0 || len == 0) {
        return false;
    }
    if (q_count[queue] == 0u) {
        return false;
    }
    n = slot_len[queue][q_head[queue]];
    head = slot_head[queue][q_head[queue]];
    if (n > dest_max) {
        drop_head(queue);
        return false;
    }
    if (expire != 0) {
        *expire = slot_expire[queue][q_head[queue]];
    }
    at = 0u;
    guard = 0u;
    while (at < n && head != BLOCK_END && guard < BLOCKS) {
        uint8_t chunk = BLOCK_DATA;
        uint8_t i;
        uint16_t remain = (uint16_t)(n - at);

        if (remain < BLOCK_DATA) {
            chunk = (uint8_t)remain;
        }
        for (i = 0u; i < chunk; ++i) {
            dest[at + i] = block[head][(uint8_t)(1u + i)];
        }
        at = (uint16_t)(at + chunk);
        head = block[head][0];
        ++guard;
    }
    *len = n;
    drop_head(queue);
    return true;
}
