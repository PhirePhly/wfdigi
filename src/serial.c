#include "wfdigi.h"

__sfr __at (PORT_SCC_B_CTRL) scc_b_ctrl;
__sfr __at (PORT_SCC_B_DATA) scc_b_data;

static uint8_t scc_b_rr0(void)
{
    uint8_t status;

    /* The sync-edge ISR also uses this channel's register pointer. */
    hardware_irq_off();
    scc_b_ctrl = 0x00u;
    status = scc_b_ctrl;
    hardware_irq_restore();
    return status;
}

void serial_putc(uint8_t byte)
{
    for (;;) {
        /* A command print can run during a keyed transmission. Keep the PTT
         * watchdog alive for that stretch, and only for that stretch. */
        if (modem_keyed()) {
            hardware_watchdog_pet();
        }
        if ((scc_b_rr0() & RR0_TX_EMPTY) != 0u) {
            break;
        }
    }
    scc_b_data = byte;
}

void serial_puts(const char *text)
{
    while (*text != '\0') {
        serial_putc((uint8_t)*text);
        ++text;
    }
}

bool serial_try_putc(uint8_t byte)
{
    if ((scc_b_rr0() & RR0_TX_EMPTY) == 0u) {
        return false;
    }
    scc_b_data = byte;
    return true;
}

bool serial_getc(uint8_t *byte)
{
    if ((scc_b_rr0() & RR0_RX_CHAR) == 0u) {
        return false;
    }
    hardware_irq_off();
    *byte = scc_b_data;
    hardware_irq_restore();
    return true;
}
