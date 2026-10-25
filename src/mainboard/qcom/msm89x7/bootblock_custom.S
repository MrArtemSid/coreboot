/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Entry point when booted by the stock Android bootloader (LK).
 *
 * The stock LK loads the whole coreboot ROM to
 * CONFIG_MSM89X7_BOOT_IMAGE_ADDR and jumps to its first byte after
 * switching the CPU to AArch64 via TrustZone (scm_elexec_call).
 *
 * The first 64 bytes of the image are the ARM64 kernel image header
 * that LK checks (magic at offset 0x38) to select the arm64 boot path;
 * the real entry code follows behind it.
 *
 * LK passes the physical address of its device tree in x0. Save it
 * before it gets clobbered.
 */

#include <arch/asm.h>

.section ".text._start", "ax"

ENTRY(_start)
	/* ARM64 kernel image header, see Linux
	 * Documentation/arch/arm64/booting.rst. Keeping image_size at 0
	 * makes the stock LK use its fixed arm64 load address without
	 * applying a text_offset. */
	b	.Lentry
	.long	0		/* code1 */
	.quad	0		/* text_offset */
	.quad	0		/* image_size */
	.quad	0		/* flags */
	.quad	0		/* res2 */
	.quad	0		/* res3 */
	.quad	0		/* res4 */
	.long	0x644d5241	/* magic "ARM\x64" */
	.long	0		/* res5 */

.Lentry:
	/* Save the DTB pointer the primary bootloader passed in x0. */
	ldr	x1, =_lk_dtb
	str	x0, [x1]

	/* Initialize CPU, exception state and stack, then run the
	 * bootblock. */
	bl	arm64_init_cpu
	bl	main
ENDPROC(_start)
