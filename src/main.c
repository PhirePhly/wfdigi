#include "wfdigi.h"
#include "version.h"

#ifndef WFDIGI_VERSION
#error WFDIGI_VERSION is injected by the Makefile
#endif

const char wfdigi_version[] = WFDIGI_VERSION;
const char wfdigi_platform[] = PLATFORM_NAME;

bool rom_checksum_pass;

static uint8_t rom_at(uint16_t addr)
{
    return *((volatile uint8_t *)addr);
}

/* Compare ROM bytes 0x0000..0x7FFD with the little-endian CRC at 0x7FFE. */
static void rom_checksum_verify(void)
{
    uint16_t addr = 0u;
    uint16_t crc = 0xFFFFu;
    uint16_t found;

    do {
        crc = crc16_feed(crc, rom_at(addr));
        ++addr;
    } while (addr != ROM_CHECK_LEN);
    found = (uint16_t)rom_at(ROM_CHECK_LEN);
    found = (uint16_t)(found | (uint16_t)((uint16_t)rom_at((uint16_t)(ROM_CHECK_LEN + 1u)) << 8));
    rom_checksum_pass = found == crc;
}

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
        if ((bit & HARDWARE_LAMP_MASK) != 0u) {
            hardware_lamps(bit);
            delay_spins(5000u);
        }
        bit = (uint8_t)(bit << 1);
    } while (bit != 0u);
    hardware_lamps(LED_CONV);
}

void firmware_boot(void)
{
    uint8_t byte;

    modem_quiesce();
    hardware_init();
    hardware_set_im2();
    serial_puts("\r\n\r\nWhiskey Fox Digi - version ");
    serial_puts(wfdigi_version);
    serial_puts(" ");
    serial_puts(wfdigi_platform);
    serial_puts("\r\nCopyright 2026 - Kenneth Finnegan\r\n");
    lamp_test();
    serial_puts("ROM checksum: ");
    rom_checksum_verify();
    serial_puts(rom_checksum_pass ? "PASS\r\n" : "FAIL\r\n");
    config_boot();
    beacon_path_reset();
    messaging_init();
    heard_init();
    timer_init();
    cli_start();
    logging_init();
    modem_init();
    telemetry_init();
    serial_rx_init();
    hardware_irq_enable();

    for (;;) {
        check_tx_interlock();
        timer_service();
        logging_service();
        modem_service();
        digi_service();
        if (serial_getc(&byte)) {
            cli_input(byte);
        }

        // Update LEDs and halt when idle
        lamps_service(true);
        hardware_halt();
        lamps_service(false);
    }
}
