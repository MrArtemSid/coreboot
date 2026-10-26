/* SPDX-License-Identifier: GPL-2.0-only */
/* Santoni diagnostic only: entry beacon and 3s WDT.
 * No GPIO, clock, pinmux or UART accesses. Not an OS-ready payload.
 */
#ifndef PAYLOAD_GENERATION
#error "Build with util/msm89x7-bootimg/build_payload.py"
#endif
.global _start
.section .text.entry, "ax"
_start:
	/* Existing reserved/NC diagnostic tail: unused u64 outside fault
	 * slots [80,a8), printk [b0,c0), and MMU/progress [c0,100).
	 * Publish before framebuffer access, without stack or console.
	 */
	ldr	x1, =0xbfffffa8
	ldr	x2, =PAYLOAD_GENERATION
	str	x2, [x1]
	dsb	sy

	/* platform_prog_run disarms the bring-up WDT. Diagnostic-only
	 * re-arm with the hardware-tested KPSS layout and 32768-Hz clock.
	 * BARK stays past BITE; never pet in the final loop.
	 */
	ldr	x1, =0x0b017000
	str	wzr, [x1, #0x08]
	mov	w2, #1
	str	w2, [x1, #0x04]
	ldr	w3, =0xfffff
	str	w3, [x1, #0x10]
	ldr	w3, =98304		/* 3000 ms */
	str	w3, [x1, #0x14]
	str	w2, [x1, #0x08]
	dsb	sy

	/* Entry-only: inherited santoni scanout is RGB888, not XRGB8888.
	 * Leave framebuffer contents and geometry entirely untouched. */
2:
	wfe
	b	2b
