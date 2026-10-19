#include "hardware.h"

const uint8_t tncid[CALLSIGN_LEN] = {'A', 'P', 'W', 'F', 'D', '2'};

__sfr __at (PORT_SIO_A_CTRL) sio_a_ctrl;
__sfr __at (PORT_SIO_A_DATA) sio_a_data;
__sfr __at (PORT_SIO_B_CTRL) sio_b_ctrl;
__sfr __at (PORT_SIO_B_DATA) sio_b_data;

/* The TNC-2 STA and CON lamps are the active-low DTR outputs in WR5. */
#define TNC2_RADIO_WR5_IDLE ((uint8_t)(0x60u | 0x01u | WR5_TX_ENABLE | WR5_DTR))
#define TNC2_TERMINAL_WR5_IDLE ((uint8_t)(0x60u | WR5_RTS | WR5_TX_ENABLE | WR5_DTR))
#define RR0_SYNC_HUNT 0x10u
#define SIO_WR1_RX_ALL 0x08u

static uint8_t radio_wr5;
static uint8_t terminal_wr5;
static uint8_t irq_live;
static uint8_t sync_state;

static void radio_reg(uint8_t reg, uint8_t value)
{
    sio_a_ctrl = reg;
    sio_a_ctrl = value;
}

static void terminal_reg(uint8_t reg, uint8_t value)
{
    sio_b_ctrl = reg;
    sio_b_ctrl = value;
}

static const uint8_t radio_setup[] = {
    4, 0x20u,                    /* SDLC, x1 external clocks */
    1, 0x00u,
    7, 0x7Eu,                    /* HDLC flag */
    5, TNC2_RADIO_WR5_IDLE,
    3, 0xC9u,                    /* 8-bit Rx, hunt, receiver enabled */
};

static const uint8_t terminal_setup[] = {
    4, 0x44u,                    /* x16, one stop bit, no parity */
    2, 0x00u,                    /* IM2 vector low byte */
    3, 0xC1u,                    /* 8-bit receiver enabled */
    5, TNC2_TERMINAL_WR5_IDLE,
    1, 0x04u,                    /* channel status modifies the IM2 vector */
};

_Static_assert((sizeof(radio_setup) & 1u) == 0u, "radio setup is register/value pairs");
_Static_assert((sizeof(terminal_setup) & 1u) == 0u, "terminal setup is register/value pairs");

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
    (void)sio_a_ctrl;
    (void)sio_b_ctrl;
    sio_a_ctrl = 0x18u;          /* channel reset */
    sio_b_ctrl = 0x18u;
    load_setup(false, radio_setup, (uint8_t)sizeof(radio_setup));
    load_setup(true, terminal_setup, (uint8_t)sizeof(terminal_setup));
    radio_wr5 = TNC2_RADIO_WR5_IDLE;
    terminal_wr5 = TNC2_TERMINAL_WR5_IDLE;
    hardware_lamps(0u);
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
        terminal_reg(5u, next);
    }
}

void hardware_equalizer(bool on)
{
    (void)on;
}

void hardware_ptt(bool keyed)
{
    uint8_t next = radio_wr5;
    if (keyed) {
        next = (uint8_t)(next | WR5_RTS);
    } else {
        next = (uint8_t)(next & (uint8_t)~WR5_RTS);
    }
    if (next != radio_wr5) {
        radio_wr5 = next;
        radio_reg(5u, next);
    }
}

void hardware_cal_tone(uint8_t tone)
{
    radio_wr5 = (uint8_t)(radio_wr5 & (uint8_t)~WR5_BREAK);
    if (tone == CAL_HIGH) {
        radio_wr5 = (uint8_t)(radio_wr5 | WR5_BREAK);
    }
    radio_reg(5u, radio_wr5);
}

void hardware_cal_restore(void)
{
    radio_wr5 = (uint8_t)(radio_wr5 & (uint8_t)~WR5_BREAK);
    radio_reg(5u, radio_wr5);
}

void hardware_lamps(uint8_t lamps_on)
{
    uint8_t next_radio;
    uint8_t next_terminal;

    hardware_irq_off();
    next_radio = radio_wr5;
    next_terminal = terminal_wr5;
    if ((lamps_on & LED_STA) != 0u) {
        next_radio = (uint8_t)(next_radio & (uint8_t)~WR5_DTR);
    } else {
        next_radio = (uint8_t)(next_radio | WR5_DTR);
    }
    if ((lamps_on & LED_CON) != 0u) {
        next_terminal = (uint8_t)(next_terminal & (uint8_t)~WR5_DTR);
    } else {
        next_terminal = (uint8_t)(next_terminal | WR5_DTR);
    }
    if (next_radio != radio_wr5) {
        radio_wr5 = next_radio;
        radio_reg(5u, next_radio);
    }
    if (next_terminal != terminal_wr5) {
        terminal_wr5 = next_terminal;
        terminal_reg(5u, next_terminal);
    }
    hardware_irq_restore();
}

bool hardware_radio_dcd(void)
{
    return modem_dcd();
}

uint8_t hardware_radio_rr0(void)
{
    sio_a_ctrl = 0u;
    return sio_a_ctrl;
}

uint8_t hardware_radio_rr1(void)
{
    sio_a_ctrl = 1u;
    return sio_a_ctrl;
}

uint8_t hardware_radio_data_read(void)
{
    return sio_a_data;
}

void hardware_radio_data_write(uint8_t value)
{
    sio_a_data = value;
}

void hardware_radio_command(uint8_t command)
{
    sio_a_ctrl = command;
}

void hardware_radio_wr1(uint8_t value)
{
    /* The SIO/0 uses both receive-interrupt mode bits for every character. */
    if ((value & 0x10u) != 0u) {
        value = (uint8_t)(value | SIO_WR1_RX_ALL);
    }
    radio_reg(1u, value);
}

uint8_t hardware_terminal_rr0(void)
{
    sio_b_ctrl = 0u;
    return sio_b_ctrl;
}

uint8_t hardware_terminal_data_read(void)
{
    return sio_b_data;
}

void hardware_terminal_data_write(uint8_t value)
{
    sio_b_data = value;
}

void hardware_terminal_command(uint8_t command)
{
    sio_b_ctrl = command;
}

void hardware_terminal_wr1(uint8_t value)
{
    /* SIO/0 WR1 bit 2 is the SCC VIS equivalent and must remain set. */
    if ((value & 0x10u) != 0u) {
        value = (uint8_t)(value | SIO_WR1_RX_ALL);
    }
    terminal_reg(1u, (uint8_t)(value | 0x04u));
}

bool hardware_timer_interrupt(void)
{
    uint8_t status = hardware_terminal_rr0();
    bool edge = ((status ^ sync_state) & RR0_SYNC_HUNT) != 0u;
    sync_state = status;
    sio_b_ctrl = 0x10u;
    sio_b_ctrl = 0x38u;
    return edge;
}

void hardware_timer_arm(void)
{
    sync_state = hardware_terminal_rr0();
    sio_b_ctrl = 0x10u;
    sio_b_ctrl = 0x10u;
    terminal_reg(1u, 0x05u);
}

void hardware_interrupt_master_enable(void)
{
    /* The Z80 SIO/0 has no SCC WR9 master-interrupt gate. */
}

void hardware_watchdog_pet(void)
{
    /* UIDIGI-compatible TNC-2 hardware has no software-serviced TX watchdog. */
}
