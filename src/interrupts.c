#include "wfdigi.h"

void modem_isr_b_tx(void);
void modem_isr_b_ext(void);
void modem_isr_b_rx(void);
void modem_isr_b_special(void);
void modem_isr_a_tx(void);
void modem_isr_a_ext(void);
void modem_isr_a_rx(void);
void modem_isr_a_special(void);

void isr_b_tx(void)
{
    modem_isr_b_tx();
}

void isr_b_ext(void)
{
    modem_isr_b_ext();
}

void isr_b_rx(void)
{
    modem_isr_b_rx();
}

void isr_b_special(void)
{
    modem_isr_b_special();
}

void isr_a_tx(void)
{
    modem_isr_a_tx();
}

void isr_a_ext(void)
{
    modem_isr_a_ext();
}

void isr_a_rx(void)
{
    modem_isr_a_rx();
}

void isr_a_special(void)
{
    modem_isr_a_special();
}
