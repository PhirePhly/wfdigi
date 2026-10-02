#include "pk88.h"

/* Master interrupt enable is left clear. These handlers exist so the mode-2
 * table at 0x0100 has a real target if a vector is ever presented.
 */
void isr_b_tx(void) {}
void isr_b_ext(void) {}
void isr_b_rx(void) {}
void isr_b_special(void) {}
void isr_a_tx(void) {}
void isr_a_ext(void) {}
void isr_a_rx(void) {}
void isr_a_special(void) {}
