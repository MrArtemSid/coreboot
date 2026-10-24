/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Reset vector for the SCR L2-family machines, whose reset PC is the
 * last page of the address space (the OCRAM ends there). Placed by the
 * board memlayout at CONFIG_SCR_RESET_STUB_ADDR - the same scheme as
 * miniboot's .reset_vector / ocram.ld - it simply jumps into the
 * bootblock entry; the linker resolves the PC-relative offset.
 */

.section .reset_vector, "ax", %progbits

.globl _scr_reset_vector
_scr_reset_vector:
	j	_start
