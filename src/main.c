#include "pk88.h"

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
    uint8_t lamps;

    hardware_init();
    hardware_set_im2();
    serial_puts("Whiskey Fox Digi - version 0.1\r\n");
    serial_puts("Copyright 2026 - Kenneth Finnegan\r\n");
    config_cold_boot();
    lamp_test();
    cli_start();

    for (;;) {
        lamps = LED_CMD;
        if (hardware_radio_dcd()) {
            lamps = (uint8_t)(lamps | LED_DCD);
        }
        hardware_lamps(lamps);

        if (serial_getc(&byte)) {
            cli_input(byte);
        }
    }
}
