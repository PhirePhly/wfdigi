#include "hardware.h"

const uint8_t tncid[CALLSIGN_LEN] = {'A', 'P', 'W', 'F', 'D', '3'};

/* Channel A is the HDLC radio. Channel B is the asynchronous terminal. */
__sfr __at (PORT_SCC_A_CTRL) radio_ctrl;
__sfr __at (PORT_SCC_A_DATA) radio_data;
__sfr __at (PORT_SCC_B_CTRL) term_ctrl;
__sfr __at (PORT_SCC_B_DATA) term_data;
__sfr __at (PORT_CIO_CTRL) cio_ctrl;

/* Z8536 registers used here. Port data is reached through the control port. */
#define CIO_MICR 0x00u
#define CIO_MCCR 0x01u
#define CIO_IVRCT 0x04u
#define CIO_DDRC 0x06u
#define CIO_CTCSR3 0x0Cu
#define CIO_PDRB 0x0Eu
#define CIO_PDRC 0x0Fu
#define CIO_CTTCR3_MSB 0x1Au
#define CIO_CTTCR3_LSB 0x1Bu
#define CIO_CTMSR3 0x1Eu
#define CIO_DDRB 0x2Bu

#define CIO_MIE 0x80u
#define CIO_RESET 0x01u
/* Port B, and counter/timer 3 together with port C. */
#define CIO_MCCR_ON ((uint8_t)(0x80u | 0x10u))
#define CIO_CT_CONTINUOUS 0x80u
/* Set IE, open the software gate, and trigger. */
#define CIO_CT_ARM 0xC6u
/* Clear IP and IUS. The gate bit stays set so the counter keeps running. */
#define CIO_CT_EOI 0x24u
/* Channel B external-status vector. Nothing drives /SYNCB, so the SCC
 * leaves that slot unused and the CIO tick takes it.
 */
#define CIO_TICK_VECTOR 0x02u

#define PC_STA 0x01u
#define PC_CON 0x02u
#define PC_MULT 0x04u
#define PB_SEND 0x08u
/* PC0-PC2 and PB3 are outputs. The other port B pins stay driven low. */
#define CIO_DDRC_LAMPS ((uint8_t)(PC_STA | PC_CON | PC_MULT))
#define CIO_DDRB_LAMPS 0xFFu

/* startup.s toggles WR5_DTR in this shadow when it pets the PTT watchdog. */
uint8_t radio_wr5;
static uint8_t term_wr5;
static uint8_t irq_live;

static const uint8_t cio_tick_tc[2] = {CIO_TICK_TC_MSB, CIO_TICK_TC_LSB};

static void cio_out(uint8_t reg, uint8_t value)
{
    cio_ctrl = reg;
    __asm
        nop
    __endasm;
    cio_ctrl = value;
    __asm
        nop
    __endasm;
}

static void cio_write(uint8_t reg, uint8_t value)
{
    hardware_irq_off();
    cio_out(reg, value);
    hardware_irq_restore();
}

static void radio_reg(uint8_t reg, uint8_t value)
{
    radio_ctrl = reg;
    radio_ctrl = value;
}

static void term_reg(uint8_t reg, uint8_t value)
{
    term_ctrl = reg;
    term_ctrl = value;
}

static void scc_reset_delay(void)
{
    volatile uint8_t spins = 255u;
    do {
        --spins;
    } while (spins != 0u);
}

static const uint8_t radio_setup[] = {
    2, 0x00u,
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

static const uint8_t term_setup[] = {
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
_Static_assert(sizeof(term_setup) <= 255u && (sizeof(term_setup) & 1u) == 0u,
               "terminal setup is register/value pairs");
_Static_assert(CIO_TICK_TC_MSB == 0x4Eu && CIO_TICK_TC_LSB == 0x20u,
               "10 ms time constant must stay 20000");
_Static_assert(TERMINAL_BRG_TC == 6u && RADIO_DPLL_TC == 30u,
               "2.4576 MHz SCC time constants");
_Static_assert(PORT_SCC_A_CTRL == 0x7Cu, "watchdog pet writes channel A control");

static void load_setup(bool terminal, const uint8_t *setup, uint8_t n)
{
    uint8_t i = 0u;
    while (i < n) {
        if (terminal) {
            term_reg(setup[i], setup[(uint8_t)(i + 1u)]);
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
    (void)radio_ctrl;
    (void)term_ctrl;
    radio_reg(9, 0xC0u);
    scc_reset_delay();
    load_setup(false, radio_setup, (uint8_t)sizeof(radio_setup));
    radio_wr5 = (uint8_t)(0x60u | 0x01u | WR5_TX_ENABLE);
    load_setup(true, term_setup, (uint8_t)sizeof(term_setup));
    term_wr5 = (uint8_t)(0x60u | WR5_RTS | WR5_TX_ENABLE);

    /* State 0 treats the first write as the master interrupt register.
     * D0 set holds reset. The following write leaves reset.
     */
    cio_ctrl = CIO_RESET;
    __asm
        nop
    __endasm;
    cio_ctrl = 0u;
    __asm
        nop
    __endasm;
    cio_out(CIO_MCCR, CIO_MCCR_ON);
    cio_out(CIO_DDRC, CIO_DDRC_LAMPS);
    cio_out(CIO_DDRB, CIO_DDRB_LAMPS);
    cio_out(CIO_PDRC, 0u);
    cio_out(CIO_PDRB, 0u);
    cio_out(CIO_CTMSR3, CIO_CT_CONTINUOUS);
    cio_out(CIO_CTTCR3_MSB, cio_tick_tc[0]);
    cio_out(CIO_CTTCR3_LSB, cio_tick_tc[1]);
    cio_out(CIO_IVRCT, CIO_TICK_VECTOR);
    hardware_lamps(0u);
}

void hardware_terminal_rts(bool ready)
{
    uint8_t next = term_wr5;
    if (ready) {
        next = (uint8_t)(next | WR5_RTS);
    } else {
        next = (uint8_t)(next & (uint8_t)~WR5_RTS);
    }
    if (next != term_wr5) {
        term_wr5 = next;
        term_reg(5, next);
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
        radio_reg(5, next);
    }
    if (keyed) {
        hardware_watchdog_pet();
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
    uint8_t port_c = 0u;
    uint8_t port_b = 0u;

    if ((lamps_on & LED_STA) != 0u) {
        port_c = (uint8_t)(port_c | PC_STA);
    }
    if ((lamps_on & LED_CON) != 0u) {
        port_c = (uint8_t)(port_c | PC_CON);
    }
    if ((lamps_on & LED_MULT) != 0u) {
        port_c = (uint8_t)(port_c | PC_MULT);
    }
    if ((lamps_on & LED_SEND) != 0u) {
        port_b = PB_SEND;
    }
    hardware_irq_off();
    cio_out(CIO_PDRC, port_c);
    cio_out(CIO_PDRB, port_b);
    hardware_irq_restore();
}

bool hardware_radio_dcd(void)
{
    return modem_dcd();
}

uint8_t hardware_radio_rr0(void)
{
    radio_ctrl = 0u;
    return radio_ctrl;
}

uint8_t hardware_radio_rr1(void)
{
    radio_ctrl = 1u;
    return radio_ctrl;
}

uint8_t hardware_radio_data_read(void)
{
    return radio_data;
}

void hardware_radio_data_write(uint8_t value)
{
    radio_data = value;
}

void hardware_radio_command(uint8_t command)
{
    radio_ctrl = command;
}

void hardware_radio_wr1(uint8_t value)
{
    radio_reg(1u, value);
}

uint8_t hardware_terminal_rr0(void)
{
    term_ctrl = 0u;
    return term_ctrl;
}

uint8_t hardware_terminal_data_read(void)
{
    return term_data;
}

void hardware_terminal_data_write(uint8_t value)
{
    term_data = value;
}

void hardware_terminal_command(uint8_t command)
{
    term_ctrl = command;
}

void hardware_terminal_wr1(uint8_t value)
{
    term_reg(1u, value);
}

bool hardware_timer_interrupt(void)
{
    cio_out(CIO_CTCSR3, CIO_CT_EOI);
    return true;
}

void hardware_timer_arm(void)
{
    cio_out(CIO_CTCSR3, CIO_CT_ARM);
}

void hardware_interrupt_master_enable(void)
{
    radio_reg(9u, 0x09u);
    cio_out(CIO_MICR, CIO_MIE);
}
