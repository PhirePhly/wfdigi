	.module isr_stubs

	.globl _isr_b_tx
	.globl _isr_b_ext
	.globl _isr_b_rx
	.globl _isr_b_special
	.globl _isr_a_tx
	.globl _isr_a_ext
	.globl _isr_a_rx
	.globl _isr_a_special

	.globl _isr_b_tx_stub
	.globl _isr_b_ext_stub
	.globl _isr_b_rx_stub
	.globl _isr_b_special_stub
	.globl _isr_a_tx_stub
	.globl _isr_a_ext_stub
	.globl _isr_a_rx_stub
	.globl _isr_a_special_stub

	.area _CODE

_isr_b_tx_stub:
	push af
	push bc
	push de
	push hl
	push ix
	push iy
	call _isr_b_tx
	jp isr_return

_isr_b_ext_stub:
	push af
	push bc
	push de
	push hl
	push ix
	push iy
	call _isr_b_ext
	jp isr_return

_isr_b_rx_stub:
	push af
	push bc
	push de
	push hl
	push ix
	push iy
	call _isr_b_rx
	jp isr_return

_isr_b_special_stub:
	push af
	push bc
	push de
	push hl
	push ix
	push iy
	call _isr_b_special
	jp isr_return

_isr_a_tx_stub:
	push af
	push bc
	push de
	push hl
	push ix
	push iy
	call _isr_a_tx
	jp isr_return

_isr_a_ext_stub:
	push af
	push bc
	push de
	push hl
	push ix
	push iy
	call _isr_a_ext
	jp isr_return

_isr_a_rx_stub:
	push af
	push bc
	push de
	push hl
	push ix
	push iy
	call _isr_a_rx
	jp isr_return

_isr_a_special_stub:
	push af
	push bc
	push de
	push hl
	push ix
	push iy
	call _isr_a_special

isr_return:
	pop iy
	pop ix
	pop hl
	pop de
	pop bc
	pop af
	ei
	reti
