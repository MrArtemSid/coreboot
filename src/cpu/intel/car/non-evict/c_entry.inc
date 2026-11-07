/* SPDX-License-Identifier: GPL-2.0-only */

	/* Setup the stack. */
	mov	$_ecar_stack, %esp

	/* Need to align stack to 16 bytes at call instruction. Account for
	the pushes below. */
	andl	$0xfffffff0, %esp

#if ENV_X86_64
	setup_longmode $PML4E

	movd	%mm2, %rdi
	shlq	$32, %rdi
	movd	%mm1, %rsi
	or	%rsi, %rdi
	movd	%mm0, %rsi

#else
	subl	$4, %esp
	/* push TSC and BIST to stack */
	movd	%mm0, %eax
	pushl	%eax	/* BIST */
	movd	%mm2, %eax
	pushl	%eax	/* tsc[63:32] */
	movd	%mm1, %eax
	pushl	%eax	/* tsc[31:0] */
#endif

	/* Copy .data section content to Cache-As-Ram */
#include <cpu/x86/copy_data_section.inc>

before_c_entry:
	post_code(POSTCODE_BOOTBLOCK_BEFORE_C_ENTRY)
	call	bootblock_c_entry_bist
