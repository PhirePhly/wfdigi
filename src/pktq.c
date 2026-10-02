#include "wfdigi.h"

/* 32-byte blocks: one link byte and 31 bytes of frame data. Index 0xFF is
 * the end of a chain and also means the free list is empty. 255 blocks let
 * short frames share the pool instead of each reserving a full MTU.
 */
#define BLOCKS 255u
#define BLOCK_BYTES 32u
#define BLOCK_DATA 31u
#define BLOCK_END 0xFFu
#define QUEUE_MAX 64u
#define FRAME_MAX 330u

static uint8_t block[BLOCKS][BLOCK_BYTES];
static uint8_t free_head;

static uint8_t slot_head[QUEUE_MAX];
static uint8_t slot_tail[QUEUE_MAX];
static uint16_t slot_len[QUEUE_MAX];
static uint8_t slot_kind[QUEUE_MAX];
static uint8_t q_head;
static uint8_t q_tail;
static uint8_t q_count;

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

void pktq_init(void)
{
    uint8_t i;

    for (i = 0u; i < (uint8_t)(BLOCKS - 1u); ++i) {
        block[i][0] = (uint8_t)(i + 1u);
    }
    block[BLOCKS - 1u][0] = BLOCK_END;
    free_head = 0u;
    q_head = 0u;
    q_tail = 0u;
    q_count = 0u;
}

bool pktq_pending(void)
{
    return q_count != 0u;
}

bool pktq_put(uint8_t kind, const uint8_t *data, uint16_t len)
{
    uint8_t head = BLOCK_END;
    uint8_t tail = BLOCK_END;
    uint16_t at = 0u;

    if (data == 0 || len == 0u || len > FRAME_MAX || q_count >= QUEUE_MAX) {
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
    slot_head[q_tail] = head;
    slot_tail[q_tail] = tail;
    slot_len[q_tail] = len;
    slot_kind[q_tail] = kind;
    q_tail = (uint8_t)((q_tail + 1u) & (QUEUE_MAX - 1u));
    ++q_count;
    return true;
}

bool pktq_take(uint8_t *kind, uint8_t *dest, uint16_t dest_max, uint16_t *len)
{
    uint8_t head;
    uint16_t n;
    uint16_t at;
    uint8_t guard;

    if (q_count == 0u || dest == 0 || len == 0) {
        return false;
    }
    n = slot_len[q_head];
    head = slot_head[q_head];
    if (n > dest_max) {
        free_chain(head);
        q_head = (uint8_t)((q_head + 1u) & (QUEUE_MAX - 1u));
        --q_count;
        return false;
    }
    if (kind != 0) {
        *kind = slot_kind[q_head];
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
    free_chain(slot_head[q_head]);
    *len = n;
    q_head = (uint8_t)((q_head + 1u) & (QUEUE_MAX - 1u));
    --q_count;
    return true;
}
