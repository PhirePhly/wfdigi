	.module startup

	.globl _firmware_boot
	.globl _firmware_reset
	.globl _hardware_set_im2
	.globl _hardware_irq_off
	.globl _hardware_irq_on
	.globl _hardware_watchdog_pet
	.globl _hardware_halt
	.globl _radio_wr5
	.globl _isr_b_tx_stub
	.globl _isr_b_ext_stub
	.globl _isr_b_rx_stub
	.globl _isr_b_special_stub
	.globl _isr_a_tx_stub
	.globl _isr_a_ext_stub
	.globl _isr_a_rx_stub
	.globl _isr_a_special_stub

	.area _HEADER (ABS)
	.org 0x0000
reset:
	di
	ld sp,#0x0000
	call _firmware_boot
halt_forever:
	halt
	jr halt_forever

_hardware_halt:
	halt
	ret

	; Channel A is the radio and channel B is the terminal, matching the
	; SCC status-low vector order. Nothing drives /SYNCB, so channel B's
	; external-status slot (0x02) is the CIO 10 ms tick.
	.org 0x0100
im2_vectors:
	.dw _isr_b_tx_stub
	.dw _isr_b_ext_stub
	.dw _isr_b_rx_stub
	.dw _isr_b_special_stub
	.dw _isr_a_tx_stub
	.dw _isr_a_ext_stub
	.dw _isr_a_rx_stub
	.dw _isr_a_special_stub

	.area _CODE

_firmware_reset:
	di
	jp 0x0000

_hardware_set_im2:
	ld a,#0x01
	ld i,a
	im 2
	ret

_hardware_irq_off:
	di
	ret

_hardware_irq_on:
	ei
	ret

	; /DTRA clocks the PTT watchdog. Preserve the caller's interrupt state
	; because this is called both from the main loop and from the TX ISR.
	; The channel A WR5 shadow is shared with the C PTT and calibration code.
_hardware_watchdog_pet:
	push bc
	push hl
	ld a,i
	di
	ld c,#0x00
	jp po,watchdog_toggle
	inc c
watchdog_toggle:
	ld hl,#_radio_wr5
	ld a,(hl)
	xor #0x80
	ld (hl),a
	ld a,#0x05
	out (0x7C),a
	ld a,(hl)
	out (0x7C),a
	ld a,c
	or a
	jr z,watchdog_done
	pop hl
	pop bc
	ei
	ret
watchdog_done:
	pop hl
	pop bc
	ret
