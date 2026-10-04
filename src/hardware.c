#include "hardware.h"

const uint8_t tncid[CALLSIGN_LEN] = {'A', 'P', 'W', 'F', 'D', 'P'};

__sfr __at (PORT_SCC_A_CTRL) scc_a_ctrl;
__sfr __at (PORT_SCC_B_CTRL) scc_b_ctrl;
__sfr __at (PORT_LED) led_latch;

/* WR5 shadows. DTR stays clear on both channels: /DTR is the 7910 MC pin,
 * and a clear bit holds that pin high. MC4 is tied high and MC3:MC2 are tied
 * low, so both DTR bits clear select equalized Bell 202.
 */
static uint8_t radio_wr5;
static uint8_t terminal_wr5;
/* Stays clear until the boot path reaches hardware_irq_enable. */
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

static void init_radio(void)
{
    radio_wr5 = (uint8_t)(0x60u | 0x01u); /* 8 bits, SDLC CRC, Tx CRC, RTS and DTR off */

    radio_reg(4, 0x20u);                         /* HDLC, x1 clock */
    radio_reg(3, 0xD8u);                         /* 8-bit Rx, hunt, Rx CRC, Rx off */
    radio_reg(5, radio_wr5);
    radio_reg(6, 0x00u);
    radio_reg(7, 0x7Eu);                         /* HDLC flag */
    radio_reg(10, 0xA0u);                        /* NRZI, flag idle, CRC preset to ones */
    radio_reg(11, 0x66u);                        /* Rx clock DPLL, Tx clock /RTxC, /TRxC = BRG */
    radio_reg(14, 0x02u);                        /* BRG source is PCLK, generator off */
    radio_reg(12, (uint8_t)RADIO_DPLL_TC);
    radio_reg(13, 0x00u);
    radio_reg(14, 0x83u);                        /* DPLL source is the BRG, generator on */
    radio_reg(14, 0xE3u);                        /* DPLL NRZI */
    radio_reg(14, 0x23u);                        /* DPLL search */
    radio_reg(3, 0xD9u);                         /* enable the receiver */
    radio_wr5 = (uint8_t)(radio_wr5 | WR5_TX_ENABLE);
    radio_reg(5, radio_wr5);                     /* enable Tx; RTS stays off so PTT is idle */
    radio_reg(0, 0x80u);                         /* reset Tx CRC; leave the underrun latch set */
    radio_reg(0, 0x40u);                         /* reset Rx CRC */
    radio_reg(15, 0xD8u);                        /* DCD, sync/hunt, underrun, break/abort */
    radio_reg(0, 0x10u);                         /* reset external/status, twice */
    radio_reg(0, 0x10u);
    radio_reg(1, 0x00u);                         /* modem_init enables HDLC receive; TX IRQ is per frame */
}

static void init_terminal(void)
{
    /* RTS on tells the host we are ready. DTR stays off so MC1 remains high. */
    terminal_wr5 = (uint8_t)(0x60u | WR5_RTS);

    terminal_reg(4, 0x44u);                      /* x16, one stop bit, no parity */
    terminal_reg(2, 0x00u);                      /* IM2 vector low byte; I is 0x01 */
    terminal_reg(3, 0xC0u);                      /* 8-bit Rx, receiver off while the BRG starts */
    terminal_reg(5, terminal_wr5);
    terminal_reg(10, 0x00u);                     /* NRZ */
    terminal_reg(11, 0x56u);                     /* both clocks from the baud-rate generator */
    terminal_reg(14, 0x02u);                     /* BRG source is PCLK, generator off */
    terminal_reg(12, (uint8_t)TERMINAL_BRG_TC);  /* 9600 baud */
    terminal_reg(13, 0x00u);
    terminal_reg(14, 0x03u);                     /* enable the generator */
    terminal_reg(3, 0xC1u);                      /* enable the receiver */
    terminal_wr5 = (uint8_t)(terminal_wr5 | WR5_TX_ENABLE);
    terminal_reg(5, terminal_wr5);
    terminal_reg(15, 0x00u);
    terminal_reg(0, 0x10u);
    terminal_reg(0, 0x10u);
    terminal_reg(1, 0x00u);
    terminal_reg(9, 0x01u);                      /* vector includes status; master interrupt stays off */
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
    radio_reg(9, 0xC0u);                         /* hardware reset of both channels */
    scc_reset_delay();

    init_radio();
    init_terminal();
    hardware_lamps(LED_CMD);
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
        radio_reg(5, radio_wr5);
    }
}

void hardware_cal_tone(uint8_t tone)
{
    uint8_t wr5 = (uint8_t)(radio_wr5 & (uint8_t)~WR5_BREAK);

    if (tone == CAL_HIGH) {
        /* NRZ mark idle, then break forces TxD low: 7910 space, 2200 Hz. */
        radio_reg(10, 0x88u);
        wr5 = (uint8_t)(wr5 | WR5_BREAK);
    } else if (tone == CAL_LOW) {
        /* NRZ mark idle holds TxD high: 7910 mark, 1200 Hz. */
        radio_reg(10, 0x88u);
    } else {
        /* Flag idle NRZI shifts 0x7E, so the modem sends both symbols. */
        radio_reg(10, 0xA0u);
    }
    radio_wr5 = wr5;
    radio_reg(5, radio_wr5);
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
    /* The radio ISR owns channel A once modem_init has run. */
    return modem_dcd();
}
