#include "wfdigi.h"

#define TICKS_PER_SECOND 100u

_Static_assert(EDGES_PER_TICK > 0u, "platform timer interrupt rate must be nonzero");

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
static uint8_t query_cool_left;
static uint16_t heard_sweep_left;

static void arm_sync_interrupt(void)
{
    hardware_timer_arm();
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
    query_cool_left = 0u;
    heard_sweep_left = HEARD_SWEEP_EVERY;
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

static void tick_on_10ms(void)
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

static void tock_on_second(void)
{
    ++clock_sec;
    if (cal_left != 0u) {
        --cal_left;
        if (cal_left == 0u) {
            modem_cal_stop();
        }
    }
    if (query_cool_left != 0u) {
        --query_cool_left;
    }
    --heard_sweep_left;
    if (heard_sweep_left == 0u) {
        heard_sweep_left = HEARD_SWEEP_EVERY;
        heard_sweep();
    }
    telemetry_second();
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

void timer_start_query_cooldown(void)
{
    query_cool_left = (uint8_t)DUPE_WINDOW;
}

bool timer_query_cooling(void)
{
    return query_cool_left != 0u;
}

void timer_service(void)
{
    uint8_t n;

    hardware_irq_off();
    n = pending;
    pending = 0u;
    hardware_irq_restore();
    while (n != 0u) {
        tick_on_10ms();
        --n;
    }
    while (seconds_pending != 0u) {
        tock_on_second();
        --seconds_pending;
    }
}
