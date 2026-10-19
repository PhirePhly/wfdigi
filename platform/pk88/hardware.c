#include "hardware.h"

const uint8_t tncid[CALLSIGN_LEN] = {'A', 'P', 'W', 'F', 'D', 'P'};

__sfr __at (PORT_SCC_A_CTRL) scc_a_ctrl;
__sfr __at (PORT_SCC_A_DATA) scc_a_data;
__sfr __at (PORT_SCC_B_CTRL) scc_b_ctrl;
__sfr __at (PORT_SCC_B_DATA) scc_b_data;
__sfr __at (PORT_LED) led_latch;

static uint8_t radio_wr5;
static uint8_t terminal_wr5;
static uint8_t irq_live;

static void radio_reg(uint8_t reg, uint8_t value)
{
    scc_a_ctrl = reg;
    scc_a_ctrl = value;
}

static void terminal_reg(uint8_t reg, uint8_t value)
{
    scc_b_ctrl = reg;
    scc_b_ctrl = value;
}

static void scc_reset_delay(void)
{
    volatile uint8_t spins = 255u;
    do {
        --spins;
    } while (spins != 0u);
}

static const uint8_t radio_setup[] = {
    4, 0x20u,
    3, 0xD8u,
    5, (uint8_t)(0x60u | 0x01u),
    6, 0x00u,
    7, 0x7Eu,
    10, 0xA8u,
    11, 0x66u,
    14, 0x02u,
    12, (uint8_t)RADIO_DPLL_TC,
    13, 0x00u,
    14, 0x83u,
    14, 0xE3u,
    14, 0x23u,
    3, 0xD9u,
    5, (uint8_t)(0x60u | 0x01u | WR5_TX_ENABLE),
    10, 0xA0u,
    0, 0x80u,
    0, 0x40u,
    15, 0xD8u,
    0, 0x10u,
    0, 0x10u,
    1, 0x00u,
};

static const uint8_t terminal_setup[] = {
    4, 0x44u,
    2, 0x00u,
    3, 0xC0u,
    5, (uint8_t)(0x60u | WR5_RTS),
    10, 0x00u,
    11, 0x56u,
    14, 0x02u,
    12, (uint8_t)TERMINAL_BRG_TC,
    13, 0x00u,
    14, 0x03u,
    3, 0xC1u,
    5, (uint8_t)(0x60u | WR5_RTS | WR5_TX_ENABLE),
    15, 0x00u,
    0, 0x10u,
    0, 0x10u,
    1, 0x00u,
    9, 0x01u,
};

_Static_assert(sizeof(radio_setup) <= 255u && (sizeof(radio_setup) & 1u) == 0u,
               "radio setup is register/value pairs");
_Static_assert(sizeof(terminal_setup) <= 255u && (sizeof(terminal_setup) & 1u) == 0u,
               "terminal setup is register/value pairs");

static void load_setup(bool terminal, const uint8_t *setup, uint8_t n)
{
    uint8_t i = 0u;
    while (i < n) {
        if (terminal) {
            terminal_reg(setup[i], setup[(uint8_t)(i + 1u)]);
        } else {
            radio_reg(setup[i], setup[(uint8_t)(i + 1u)]);
        }
        i = (uint8_t)(i + 2u);
    }
}

void hardware_irq_enable(void)
{
    irq_live = 1u;
    hardware_irq_on();
}

void hardware_irq_restore(void)
{
    if (irq_live != 0u) {
        hardware_irq_on();
    }
}

void hardware_init(void)
{
    irq_live = 0u;
    (void)scc_a_ctrl;
    (void)scc_b_ctrl;
    radio_reg(9, 0xC0u);
    scc_reset_delay();
    load_setup(false, radio_setup, (uint8_t)sizeof(radio_setup));
    radio_wr5 = (uint8_t)(0x60u | 0x01u | WR5_TX_ENABLE);
    load_setup(true, terminal_setup, (uint8_t)sizeof(terminal_setup));
    terminal_wr5 = (uint8_t)(0x60u | WR5_RTS | WR5_TX_ENABLE);
    hardware_lamps(LED_CMD);
}

void hardware_terminal_rts(bool ready)
{
    uint8_t next = terminal_wr5;
    if (ready) {
        next = (uint8_t)(next | WR5_RTS);
    } else {
        next = (uint8_t)(next & (uint8_t)~WR5_RTS);
    }
    if (next != terminal_wr5) {
        terminal_wr5 = next;
        terminal_reg(5, next);
    }
}

void hardware_equalizer(bool on)
{
    uint8_t next = radio_wr5;
    if (on) {
        next = (uint8_t)(next & (uint8_t)~WR5_DTR);
    } else {
        next = (uint8_t)(next | WR5_DTR);
    }
    if (next != radio_wr5) {
        radio_wr5 = next;
        radio_reg(5, next);
    }
}

void hardware_ptt(bool keyed)
{
    uint8_t next = radio_wr5;
    if (keyed) {
        next = (uint8_t)(next | WR5_RTS);
        hardware_watchdog_pet();
    } else {
        next = (uint8_t)(next & (uint8_t)~WR5_RTS);
    }
    if (next != radio_wr5) {
        radio_wr5 = next;
        radio_reg(5, next);
    }
}

void hardware_cal_tone(uint8_t tone)
{
    uint8_t wr5 = (uint8_t)(radio_wr5 & (uint8_t)~WR5_BREAK);
    if (tone == CAL_HIGH) {
        radio_reg(10, 0x88u);
        wr5 = (uint8_t)(wr5 | WR5_BREAK);
    } else if (tone == CAL_LOW) {
        radio_reg(10, 0x88u);
    } else {
        radio_reg(10, 0xA0u);
    }
    radio_wr5 = wr5;
    radio_reg(5, wr5);
}

void hardware_cal_restore(void)
{
    radio_wr5 = (uint8_t)(radio_wr5 & (uint8_t)~WR5_BREAK);
    radio_reg(5, radio_wr5);
    radio_reg(10, 0xA0u);
}

void hardware_lamps(uint8_t lamps_on)
{
    led_latch = (uint8_t)~lamps_on;
}

bool hardware_radio_dcd(void)
{
    return modem_dcd();
}

uint8_t hardware_radio_rr0(void)
{
    scc_a_ctrl = 0u;
    return scc_a_ctrl;
}

uint8_t hardware_radio_rr1(void)
{
    scc_a_ctrl = 1u;
    return scc_a_ctrl;
}

uint8_t hardware_radio_data_read(void)
{
    return scc_a_data;
}

void hardware_radio_data_write(uint8_t value)
{
    scc_a_data = value;
}

void hardware_radio_command(uint8_t command)
{
    scc_a_ctrl = command;
}

void hardware_radio_wr1(uint8_t value)
{
    radio_reg(1u, value);
}

uint8_t hardware_terminal_rr0(void)
{
    scc_b_ctrl = 0u;
    return scc_b_ctrl;
}

uint8_t hardware_terminal_data_read(void)
{
    return scc_b_data;
}

void hardware_terminal_data_write(uint8_t value)
{
    scc_b_data = value;
}

void hardware_terminal_command(uint8_t command)
{
    scc_b_ctrl = command;
}

void hardware_terminal_wr1(uint8_t value)
{
    terminal_reg(1u, value);
}

bool hardware_timer_interrupt(void)
{
    (void)hardware_terminal_rr0();
    scc_b_ctrl = 0x10u;
    scc_b_ctrl = 0x38u;
    return true;
}

void hardware_timer_arm(void)
{
    terminal_reg(15u, 0x10u);
    scc_b_ctrl = 0x10u;
    scc_b_ctrl = 0x10u;
    terminal_reg(1u, 0x01u);
}

void hardware_interrupt_master_enable(void)
{
    radio_reg(9u, 0x09u);
}
