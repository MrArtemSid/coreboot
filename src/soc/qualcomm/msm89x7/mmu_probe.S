/* SPDX-License-Identifier: GPL-2.0-only */
/* Temporary EL1 bring-up probe. Never enables D-cache. */
#include <arch/asm.h>
#include <arch/lib_helpers.h>

/* Mode in x0:
 * 0: Instruction fetch only (branches to .Lprobe_off immediately)
 * 1: Data read ONLY (ldr x14, [x1])
 * 2: Data write ONLY (str x14, [x1])
 * 3: Stack read ONLY (ldr x15, [sp])
 * 4: Stack write ONLY (str x15, [sp, #-16])
 * 5: Framebuffer write ONLY (strb w14, [x1])
 *
 * x1: target pointer
 * Caller must have M=C=0 and identity mappings.
 */
ENTRY(msm89x7_mmu_probe)
	/* Bit 8 also enables D-cache; low byte selects the access. */
	and	x12, x0, #0xff
	mrs	x9, sctlr_el1
	mrs	x10, vbar_el1
	mrs	x13, daif
	msr	daifset, #0xf
	adr	x11, msm89x7_probe_vectors
	msr	vbar_el1, x11
	dsb	sy
	isb
	tlbi	vmalle1
	dsb	sy
	isb
	bic	x14, x9, #SCTLR_C
	orr	x14, x14, #SCTLR_M
	tbz	x0, #8, .Lprobe_cache_ready
	orr	x14, x14, #SCTLR_C
.Lprobe_cache_ready:
	msr	sctlr_el1, x14
	isb

	/* Bit 9: spend 100 ms with translation enabled, without data
	 * accesses, so a delayed reset cannot masquerade as printk failure. */
	tbz	x0, #9, .Lprobe_select_access
	mrs	x2, cntfrq_el0
	mov	x3, #10
	udiv	x2, x2, x3
	mrs	x3, cntpct_el0
.Lprobe_delay:
	mrs	x4, cntpct_el0
	sub	x4, x4, x3
	cmp	x4, x2
	b.lo	.Lprobe_delay
	b	.Lprobe_off
.Lprobe_select_access:
	cbz	x12, .Lprobe_off
	cmp	x12, #1
	b.eq	.Lprobe_read
	cmp	x12, #2
	b.eq	.Lprobe_write
	cmp	x12, #3
	b.eq	.Lprobe_stack_read
	cmp	x12, #4
	b.eq	.Lprobe_stack_write
	cmp	x12, #5
	b.eq	.Lprobe_fb_write
	b	.Lprobe_off

.Lprobe_read:
	ldr	x14, [x1]
	b	.Lprobe_off

.Lprobe_write:
	mov	x14, #0x55aa
	str	x14, [x1]
	b	.Lprobe_off

.Lprobe_stack_read:
	ldr	x15, [sp]
	b	.Lprobe_off

.Lprobe_stack_write:
	ldr	x15, [sp]
	str	x15, [sp, #-16]
	b	.Lprobe_off

.Lprobe_fb_write:
	ldrb	w14, [x1]
	strb	w14, [x1]
	b	.Lprobe_off

.Lprobe_off:
	dsb	sy
	msr	sctlr_el1, x9
	isb
	msr	vbar_el1, x10
	isb
	msr	daif, x13
	ret
ENDPROC(msm89x7_mmu_probe)

/* Install the cache-off first-fault vector across the normal C switch. */
ENTRY(msm89x7_mmu_capture_vectors)
	msr	daifset, #0xf
	adr	x0, msm89x7_probe_vectors
	msr	vbar_el1, x0
	isb
	ret
ENDPROC(msm89x7_mmu_capture_vectors)

ENTRY(msm89x7_mmu_live_probe)
	/* CB29: zero result slots so an actual all-ones read is distinguishable
	 * from an instruction that was never reached. */
	str	x30, [sp, #-16]!
	ldr	x0, =0xbfffffc0
	str	x30, [x0]
	str	xzr, [x0, #8]
	str	xzr, [x0, #16]
	str	xzr, [x0, #24]
	str	xzr, [x0, #40]
	str	xzr, [x0, #48]
	str	xzr, [x0, #56]
	mov	x2, sp
	str	x2, [x0, #32]
	mrs	x9, sctlr_el1
	orr	x14, x9, #SCTLR_M
	orr	x14, x14, #SCTLR_C
	dsb	sy
	msr	sctlr_el1, x14
	isb
	str	x30, [x0, #8]
	ldr	x2, [sp]
	str	x2, [x0, #16]
	mov	x3, #1
	str	x3, [x0, #48]
	ldr	x2, [sp, #16]
	str	x2, [x0, #24]
	mov	x3, #2
	str	x3, [x0, #48]
	dsb	sy
	msr	sctlr_el1, x9
	isb
	mov	x3, #3
	str	x3, [x0, #48]
	add	sp, sp, #16
	ret
ENDPROC(msm89x7_mmu_live_probe)

ENTRY(msm89x7_mmu_switch_probe)
	mov	x1, x0
	mrs	x9, sctlr_el1
	mrs	x10, vbar_el1
	mrs	x13, daif
	msr	daifset, #0xf
	adr	x11, msm89x7_probe_vectors
	msr	vbar_el1, x11
	dsb	sy
	isb
	tlbi	vmalle1
	dsb	sy
	isb
	bic	x14, x9, #SCTLR_C
	orr	x14, x14, #SCTLR_M
	orr	x14, x14, #SCTLR_I
	msr	sctlr_el1, x14
	isb

	/* Under MMU (M=1, C=0, I=1): write token to scratch and read it back */
	mov	x2, #0x5678
	movk	x2, #0x1234, lsl #16
	str	x2, [x1]
	ldr	x0, [x1]

	dsb	sy
	msr	sctlr_el1, x9
	isb
	msr	vbar_el1, x10
	isb
	msr	daif, x13
	ret
ENDPROC(msm89x7_mmu_switch_probe)

.macro probe_vector id
	.p2align 7
	mov	x0, #\id
	b	.Lprobe_fault
.endm

ENTRY_WITH_ALIGN(msm89x7_probe_vectors, 11)
probe_vector 0
probe_vector 1
probe_vector 2
probe_vector 3
probe_vector 4
probe_vector 5
probe_vector 6
probe_vector 7
probe_vector 8
probe_vector 9
probe_vector 10
probe_vector 11
probe_vector 12
probe_vector 13
probe_vector 14
probe_vector 15
	.p2align 7
ENDPROC(msm89x7_probe_vectors)

.Lprobe_fault:
	mrs	x1, esr_el1
	mrs	x2, far_el1
	mrs	x3, elr_el1
	mrs	x4, sctlr_el1
	bic	x5, x4, #SCTLR_M
	bic	x5, x5, #SCTLR_C
	msr	sctlr_el1, x5
	isb
	msr	daifset, #0xf
	/* Raw first-fault record precedes C, stack and framebuffer access. */
	mov	x6, #0xff80
	movk	x6, #0xbfff, lsl #16
	stp	x1, x2, [x6]
	stp	x3, x4, [x6, #16]
	str	x0, [x6, #32]
	dsb	sy
	ldr	x6, =_estack
	sub	x6, x6, #16
	mov	sp, x6
	b	msm89x7_mmu_probe_fault
