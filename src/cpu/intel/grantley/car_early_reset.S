/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * Grantley-specific replacement for src/cpu/x86/early_reset.S.
 *
 * The generic routine treats a dirty IA32_MTRR_DEF_TYPE as proof of an
 * unclean (INIT#) boot, writes RCBA/CF9 <- 0x06 and then executes HLT
 * forever.  On the X99-QD4 that is a dead end: the board has no POST
 * display and no serial console, so a platform reset that does not assert
 * (or one that leaves MTRR_DEF_TYPE dirty) is indistinguishable from a
 * board that never starts.  The stock image does not reset here either.
 *
 * Stock evidence (QD4V10.bin, SEC, VA 0xFFFFEFE1):
 *	b9 ff 02 00 00	mov    $0x2ff,%ecx
 *	0f 32		rdmsr
 *	25 00 f3 ff ff	and    $0xfffff300,%eax
 *	0f 30		wrmsr
 *	b8 08 00 00 80	mov    $0x80000008,%eax	<-- falls through
 * i.e. the stock clears MTRR_DEF_TYPE[EN|FIX_EN] and the default memory type
 * in DEF_TYPE[7:0] (leaving UC), then continues.  It does not reset here.
 * The only CF9 write in the whole 16 MiB stock image (VA 0xFFFFF531) belongs
 * to a terminal error block whose both branches end in `jmp $`; the stock
 * image never "resets and continues".  Neither does this routine: a reset issued
 * from inside CAR (FULL_RST also resets the ME) would be an asynchronous
 * reset with no stock equivalent, and no bounded delay can observe PLTRST,
 * which follows milliseconds after the write.
 *
 * Continuing is safe for this stage because the CAR entry that runs next
 * erases all MTRR state before caching is enabled: every fixed MTRR is
 * written to zero, every variable MTRR is cleared, only CAR + the ROM
 * range are programmed, and INVD is executed before CR0.CD is cleared.
 * A stale memory type therefore cannot leak into the CAR/bootblock stage.
 *
 * input %esp: return address (not pointer to return address!)
 * clobber the content of eax, ecx, edx
 */

#include <cpu/x86/mtrr.h>

.code32
.section .text
.global check_mtrr

check_mtrr:
check_for_clean_reset:
	/* MTRR_DEF_TYPE is the proxy for detecting INIT#. */
	movl	$MTRR_DEF_TYPE_MSR, %ecx
	rdmsr
	testl	$(MTRR_DEF_TYPE_EN | MTRR_DEF_TYPE_FIX_EN), %eax
	jz	.Lenter_car

	/* Stock parity: clear EN, FIX_EN and DEF_TYPE[7:0] (default type -> UC),
	 * then continue.  No reset and no HLT on this path. */
	andl	$0xfffff300, %eax
	wrmsr

.Lenter_car:
	jmp	*%esp
