#include "wfdigi.h"

/* Room past the high-water mark absorbs characters already on the wire
 * after RTS drops. The service loop raises RTS again once it has drained
 * the buffer to the low-water mark.
 */
#define RX_SIZE 128u
#define RX_HIGH 64u
#define RX_LOW 16u

_Static_assert((RX_SIZE & (RX_SIZE - 1u)) == 0u, "receive buffer is a power of two");
_Static_assert(RX_LOW < RX_HIGH && RX_HIGH < RX_SIZE, "receive flow-control marks");

__sfr __at (PORT_SCC_B_CTRL) scc_b_ctrl;
__sfr __at (PORT_SCC_B_DATA) scc_b_data;

static volatile uint8_t rx_ring[RX_SIZE];
static volatile uint8_t rx_head;
static volatile uint8_t rx_tail;
static volatile uint8_t rx_count;
static volatile uint8_t rx_paused;

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

void serial_flush(void)
{
    for (;;) {
        if (modem_keyed()) {
            hardware_watchdog_pet();
        }
        if ((scc_b_rr0() & RR0_TX_EMPTY) != 0u) {
            return;
        }
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

void serial_rx_init(void)
{
    uint8_t guard = 0u;

    rx_head = 0u;
    rx_tail = 0u;
    rx_count = 0u;
    rx_paused = 0u;
    hardware_terminal_rts(true);
    /* External/status stays enabled for /SYNCB. Also interrupt on every byte. */
    scc_b_ctrl = 0x01u;
    scc_b_ctrl = 0x11u;
    scc_b_ctrl = 0x00u;
    while ((scc_b_ctrl & RR0_RX_CHAR) != 0u && guard < 4u) {
        serial_rx_push(scc_b_data);
        scc_b_ctrl = 0x00u;
        ++guard;
    }
}

void serial_rx_push(uint8_t byte)
{
    if (rx_count >= RX_SIZE) {
        return;
    }
    rx_ring[rx_head] = byte;
    rx_head = (uint8_t)((rx_head + 1u) & (RX_SIZE - 1u));
    ++rx_count;
    if (rx_count >= RX_HIGH) {
        rx_paused = 1u;
        hardware_terminal_rts(false);
    }
}

bool serial_getc(uint8_t *byte)
{
    if (rx_count == 0u) {
        return false;
    }
    hardware_irq_off();
    if (rx_count == 0u) {
        hardware_irq_restore();
        return false;
    }
    *byte = rx_ring[rx_tail];
    rx_tail = (uint8_t)((rx_tail + 1u) & (RX_SIZE - 1u));
    --rx_count;
    if (rx_paused != 0u && rx_count <= RX_LOW) {
        rx_paused = 0u;
        hardware_terminal_rts(true);
    }
    hardware_irq_restore();
    return true;
}
