#include "wfdigi.h"
#include "version.h"

#ifndef WFDIGI_VERSION
#error WFDIGI_VERSION is injected by the Makefile
#endif

const char wfdigi_version[] = WFDIGI_VERSION;

static void delay_spins(uint16_t spins)
{
    volatile uint16_t left = spins;
    while (left != 0u) {
        --left;
    }
}

static void lamp_test(void)
{
    uint8_t bit = LED_CONV;
    do {
        hardware_lamps(bit);
        delay_spins(5000u);
        bit = (uint8_t)(bit << 1);
    } while (bit != 0u);
    hardware_lamps(LED_CMD);
}

void firmware_boot(void)
{
    uint8_t byte;

    modem_quiesce();
    hardware_init();
    hardware_set_im2();
    serial_puts("\r\n\r\nWhiskey Fox Digi - version ");
    serial_puts(wfdigi_version);
    serial_puts("\r\n");
    serial_puts("Copyright 2026 - Kenneth Finnegan\r\n");
    lamp_test();
    config_boot();
    timer_init();
    cli_start();
    modem_init();
    telemetry_init();
    serial_rx_init();
    hardware_irq_enable();

    for (;;) {
        check_tx_interlock();
        timer_service();
        modem_service();
        lamps_service();

        if (serial_getc(&byte)) {
            cli_input(byte);
        }
    }
}
