#include "pk88.h"

__sfr __at (PORT_SCC_B_CTRL) scc_b_ctrl;
__sfr __at (PORT_SCC_B_DATA) scc_b_data;

void serial_putc(uint8_t byte)
{
    for (;;) {
        scc_b_ctrl = 0x00u;
        if ((scc_b_ctrl & RR0_TX_EMPTY) != 0u) {
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

bool serial_getc(uint8_t *byte)
{
    scc_b_ctrl = 0x00u;
    if ((scc_b_ctrl & RR0_RX_CHAR) == 0u) {
        return false;
    }
    *byte = scc_b_data;
    return true;
}
