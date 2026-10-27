#ifndef PLATFORM_H
#define PLATFORM_H

#define PLATFORM_NAME "pk232"
/* Battery SRAM is the top 16 KiB. The config page is its first 256 bytes. */
#define CONFIG_ADDR 0xC000u
#define COMPILER_RAM 0xC100u
#define ROM_CHECK_LEN 0x7FFEu
/* STA, CON, and MULT are port C. SEND is PB3. The decoded port B groups are unused. */
#define HARDWARE_LAMP_MASK ((uint8_t)(LED_STA | LED_CON | LED_MULT | LED_SEND))

/* CT3 is programmed for one interrupt every 10 ms. */
#define EDGES_PER_TICK 1u

/* The CPU and the CIO PCLK are 4 MHz. Timer mode counts PCLK/2, so 10 ms is
 * 20000 counts (0x4E20). These are literals because SDCC evaluates preprocessor
 * arithmetic in 16 bits.
 */
#define CIO_TICK_TC_MSB 0x4Eu
#define CIO_TICK_TC_LSB 0x20u

/* SCC PCLK is 2.4576 MHz, separate from the 4 MHz CPU and CIO clock.
 * 9600 8N1 x16 is time constant 6. The 1200 baud DPLL wants a 32x clock,
 * which is time constant 30. Both divide the SCC clock exactly.
 */
#define TERMINAL_BRG_TC 6u
#define RADIO_DPLL_TC 30u

/* Channel A is the radio. Channel B is the terminal. */
#define PORT_SCC_A_CTRL 0x7Cu
#define PORT_SCC_A_DATA 0x7Du
#define PORT_SCC_B_CTRL 0x7Eu
#define PORT_SCC_B_DATA 0x7Fu
#define PORT_CIO_CTRL 0xBFu

#endif
