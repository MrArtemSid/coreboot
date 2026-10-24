/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Reset stub for the SCR L2-family machines, whose reset PC is the last
 * page of the address space (CONFIG_SCR_RESET_STUB_ADDR): the 64 KiB
 * OCRAM ends there, so the bootblock entry at OCRAM + 0x200 is 0xFD00
 * bytes below it - a constant of the memlayout, independent of the ISA
 * width. Assembled into a standalone 4-byte binary (see the Makefile.mk
 * rule) that the boot ROM / FPGA preloader / SCR QEMU loader places at
 * the reset PC; it simply jumps into the bootblock.
 */

.section .text, "ax", %progbits

.globl _scr_reset_stub
_scr_reset_stub:
	j	.-STUB_DELTA
