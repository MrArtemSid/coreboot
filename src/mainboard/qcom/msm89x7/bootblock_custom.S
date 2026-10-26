/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Entry point when booted by the stock Android bootloader (LK).
 *
 * LK may load the whole coreboot ROM at a different address from
 * CONFIG_MSM89X7_BOOT_IMAGE_ADDR before switching the CPU to AArch64
 * via TrustZone (scm_elexec_call). Move it to its linked address before
 * entering C: the bootblock and CBFS contain absolute pointers.
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
	/* arm64_init_cpu clobbers x0; preserve the incoming DTB in x20.
	 * It disables MMU/D-cache and initializes the linked stack without
	 * calling C, so the copy below runs with known cache state. */
	mov	x20, x0
	bl	arm64_init_cpu

	adr	x1, _start
	ldr	x2, =CONFIG_MSM89X7_BOOT_IMAGE_ADDR
	cmp	x1, x2
	b.eq	.Llinked_entry

	/* Copy the entire ROM, not just the bootblock: media.c accesses
	 * the FMAP/CBFS through the linked base as well. Do not copy the
	 * appended DTB: LK has already placed the handoff DTB at x20. */
	ldr	x3, =CONFIG_ROM_SIZE

	/* An overlapping move could overwrite the code executing this
	 * loop. Refuse it; the observed 0x40080000 -> 0x80080000 move
	 * and the already-correct load address do not overlap. */
	add	x4, x1, x3
	cmp	x2, x4
	b.hs	.Lcopy_rom
	add	x4, x2, x3
	cmp	x1, x4
	b.lo	.Loverlap_halt

.Lcopy_rom:
	ldp	x5, x6, [x1], #16
	stp	x5, x6, [x2], #16
	subs	x3, x3, #16
	b.ne	.Lcopy_rom

	/* D-cache is off. Complete the stores and discard stale fetched
	 * instructions before branching into the copied bootblock. */
	dsb	sy
	ic	iallu
	dsb	sy
	isb
	ldr	x1, =.Llinked_entry
	br	x1

.Loverlap_halt:
	b	.Loverlap_halt

.Llinked_entry:
	ldr	x1, =_lk_dtb
	str	x20, [x1]
	bl	main
ENDPROC(_start)
