#ifndef PLATFORM_H
#define PLATFORM_H

#define PLATFORM_NAME "tnc2"
#define CONFIG_ADDR 0x8000u
#define ROM_CHECK_LEN 0x7FFEu
/* The two SIO DTR-driven lamps display STA and CON. */
#define HARDWARE_LAMP_MASK ((uint8_t)(LED_STA | LED_CON))

/* The SIO timer interrupt runs at 600 Hz: 6 interrupts make one 10 ms tick. */
#define EDGES_PER_TICK 12u

#define PORT_SIO_A_DATA 0xDCu
#define PORT_SIO_A_CTRL 0xDDu
#define PORT_SIO_B_DATA 0xDEu
#define PORT_SIO_B_CTRL 0xDFu

#endif
