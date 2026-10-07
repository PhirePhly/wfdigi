#include "wfdigi.h"

/* Paint the front panel from the current radio, queue, and interlock state. */
void lamps_service(void)
{
    uint8_t lamps = 0u;

    if (!tx_interlock || timer_blink()) {
        lamps = LED_CMD;
    }
    if (hardware_radio_dcd()) {
        lamps = (uint8_t)(lamps | LED_DCD);
    }
    if (modem_keyed()) {
        lamps = (uint8_t)(lamps | LED_SEND);
    }
    if (timer_running(TIMER_STA)) {
        lamps = (uint8_t)(lamps | LED_STA);
    }
    if (pktq_pending(PKTQ_TX)) {
        lamps = (uint8_t)(lamps | LED_MULT);
    }
    if (pktq_pending(PKTQ_VISCOUS)) {
        lamps = (uint8_t)(lamps | LED_CON);
    }
    hardware_lamps(lamps);
}
