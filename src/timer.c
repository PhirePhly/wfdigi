#include "wfdigi.h"

/* The /SYNCB interrupt runs at 1200 Hz, so 12 interrupts are 10 ms. */
#define EDGES_PER_TICK 12u
#define TICKS_PER_SECOND 100u

__sfr __at (PORT_SCC_B_CTRL) scc_b_ctrl;

/* Countdown timers in 10 ms units. A uint16 covers a little over 10 minutes. */
static uint16_t left[TIMER_COUNT];
static uint8_t armed[TIMER_COUNT];
static uint8_t fired[TIMER_COUNT];
static volatile uint8_t edges;
static volatile uint8_t pending;
static uint8_t subsec;
static uint8_t seconds_pending;
static uint16_t clock_sec;
static uint16_t beacon_left;
static uint8_t cal_left;

static void arm_sync_interrupt(void)
{
    scc_b_ctrl = 0x0Fu;
    scc_b_ctrl = 0x10u; /* WR15: Sync/Hunt only */
    scc_b_ctrl = 0x10u; /* drop a stale external-status latch */
    scc_b_ctrl = 0x10u;
    scc_b_ctrl = 0x01u;
    scc_b_ctrl = 0x01u; /* WR1: external/status interrupt */
}

void timer_init(void)
{
    uint8_t i;

    edges = 0u;
    pending = 0u;
    subsec = 0u;
    seconds_pending = 0u;
    clock_sec = 0u;
    beacon_left = 0u;
    cal_left = 0u;
    prng_init();
    for (i = 0u; i < TIMER_COUNT; ++i) {
        left[i] = 0u;
        armed[i] = 0u;
        fired[i] = 0u;
    }
    beacon_left = beacon_next_wait();
    arm_sync_interrupt();
}

void timer_sync_edge(void)
{
    ++edges;
    if (edges < EDGES_PER_TICK) {
        return;
    }
    edges = 0u;
    if (pending != 255u) {
        ++pending;
    }
}

void timer_set(uint8_t slot, uint16_t ticks_10ms)
{
    if (slot >= TIMER_COUNT) {
        return;
    }
    fired[slot] = 0u;
    left[slot] = ticks_10ms;
    armed[slot] = ticks_10ms != 0u ? 1u : 0u;
}

bool timer_running(uint8_t slot)
{
    if (slot >= TIMER_COUNT) {
        return false;
    }
    return armed[slot] != 0u;
}

bool timer_expired(uint8_t slot)
{
    if (slot >= TIMER_COUNT || fired[slot] == 0u) {
        return false;
    }
    fired[slot] = 0u;
    return true;
}

static void tick(void)
{
    uint16_t *count = left;
    uint8_t i;

    for (i = 0u; i < TIMER_COUNT; ++i) {
        if (armed[i] != 0u && *count != 0u) {
            --*count;
            if (*count == 0u) {
                armed[i] = 0u;
                fired[i] = 1u;
            }
        }
        ++count;
    }
    ++subsec;
    if (subsec < TICKS_PER_SECOND) {
        return;
    }
    subsec = 0u;
    if (seconds_pending != 255u) {
        ++seconds_pending;
    }
}

static void on_second(void)
{
    ++clock_sec;
    if (cal_left != 0u) {
        --cal_left;
        if (cal_left == 0u) {
            modem_cal_stop();
        }
    }
    if (g_config.beacon_every == 0u) {
        return;
    }
    if (beacon_left != 0u) {
        --beacon_left;
    }
    if (beacon_left != 0u) {
        return;
    }
    if (beacon_send()) {
        beacon_left = beacon_next_wait();
    }
}

uint16_t timer_seconds(void)
{
    return clock_sec;
}

bool timer_blink(void)
{
    /* subsec counts 10 ms ticks, 0 through 99, then restarts. */
    return subsec < 25u || (subsec >= 50u && subsec < 75u);
}

bool timer_in_dupe_window(uint16_t heard_at)
{
    return (uint16_t)(clock_sec - heard_at) < DUPE_WINDOW;
}

void timer_beacon_restart(void)
{
    beacon_left = beacon_next_wait();
}

bool timer_beacon_now(void)
{
    if (!beacon_send()) {
        beacon_left = 0u;
        return false;
    }
    beacon_left = beacon_next_wait();
    return true;
}

void timer_cal_start(uint8_t seconds)
{
    cal_left = seconds;
}

void timer_service(void)
{
    uint8_t n;

    hardware_irq_off();
    n = pending;
    pending = 0u;
    hardware_irq_restore();
    while (n != 0u) {
        tick();
        --n;
    }
    while (seconds_pending != 0u) {
        on_second();
        --seconds_pending;
    }
}
