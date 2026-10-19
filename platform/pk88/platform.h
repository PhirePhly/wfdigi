#ifndef PLATFORM_H
#define PLATFORM_H

#define PLATFORM_NAME "pk88"
#define CONFIG_ADDR 0x8000u
#define ROM_CHECK_LEN 0x7FFEu
#define HARDWARE_LAMP_MASK 0xFFu
/* /SYNCB generates 1200 interrupts per second: 12 make one 10 ms tick. */
#define EDGES_PER_TICK 12u

/* Z80 and SCC PCLK are both 4.9152 MHz. These are literals because SDCC
 * evaluates preprocessor arithmetic in 16 bits.
 */
#define TERMINAL_BRG_TC 14u
#define RADIO_DPLL_TC 62u

#define PORT_SCC_B_CTRL 0xF0u
#define PORT_SCC_B_DATA 0xF1u
#define PORT_SCC_A_CTRL 0xF2u
#define PORT_SCC_A_DATA 0xF3u
#define PORT_LED 0xF4u

#endif
