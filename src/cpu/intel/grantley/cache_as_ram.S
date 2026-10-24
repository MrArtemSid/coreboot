/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/intel/post_codes.h>
#include <cpu/x86/mtrr.h>
#include <cpu/x86/cache.h>
#include <cpu/x86/post_code.h>

#define NoEvictMod_MSR 0x2e0

.section .init
.global bootblock_pre_c_entry

#include <cpu/intel/car/cache_as_ram_symbols.inc>

.code32
_cache_as_ram_setup:

bootblock_pre_c_entry:
	movl	$cache_as_ram, %esp /* return address */
	jmp	check_mtrr /* Check if CPU properly reset */

cache_as_ram:
	/* Enable FXSAVE and unmasked SIMD exception support (CR4.OSFXSR |
	 * OSXMMEXCPT).  The stock SEC enables these before any IO or C code;
	 * the Uncore PEIM uses XMM registers and would fault without them. */
	movl	%cr4, %eax
	orl	$((1 << 9) | (1 << 10)), %eax
	movl	%eax, %cr4

	/* Program the early SAD I/O and MMIO target lists via CF8/CFC. */
	/* Set LOCKCONTROL.LockDisable (1:16.5:0x50 bit 0). */
	movl	$0x80018550, %eax
	movw	$0xcf8, %dx
	outl	%eax, %dx
	movw	$0xcfc, %dx
	inl	%dx, %eax
	orl	$1, %eax
	outl	%eax, %dx

	/* 2. Route IO Target List to Legacy Node (1:15:5:0xe0) */
	movl	$0x80017df0, %eax	/* SAD_TARGET_CBO */
	movw	$0xcf8, %dx
	outl	%eax, %dx
	movw	$0xcfc, %dx
	inl	%dx, %eax
	andl	$0x38, %eax		/* keep bits 5:3 */
	shrl	$3, %eax		/* node id */
	movl	%eax, %ebx
	movb	$3, %cl
	movb	$7, %ch
1:
	shll	%cl, %eax
	orl	%eax, %ebx
	decb	%ch
	jnz	1b

	movl	$0x80017de0, %eax	/* IOPORT_TARGET_LIST_CBO */
	movw	$0xcf8, %dx
	outl	%eax, %dx
	movw	$0xcfc, %dx
	movl	%ebx, %eax
	outl	%eax, %dx

	movl	$0x80017de8, %eax	/* MMIO_TARGET_LIST_CBO */
	movw	$0xcf8, %dx
	outl	%eax, %dx
	movw	$0xcfc, %dx
	movl	%ebx, %eax
	outl	%eax, %dx

	/* Decode 0xfc000000..0xffffffff, including CAR, as MMIO (1:15.5). */
	movl	$0x80017d5c, %eax
	movw	$0xcf8, %dx
	outl	%eax, %dx
	movw	$0xcfc, %dx
	xorl	%eax, %eax
	outl	%eax, %dx

	movl	$0x80017d58, %eax
	movw	$0xcf8, %dx
	outl	%eax, %dx
	movw	$0xcfc, %dx
	movl	$0xff00007f, %eax
	outl	%eax, %dx

	/* Entry16/32 POSTs stay suppressed until legacy routing is installed.
	 * Restore assembly POSTs here, including the CAR/VCU failure paths.
	 */
#undef post_code
#if CONFIG(POST_IO)
#define post_code(value) movb $value, %al; outb %al, $CONFIG_POST_IO_PORT
#else
#define post_code(value)
#endif
	post_code(POSTCODE_BOOTBLOCK_CAR)

#define GRANTLEY_PRE_NEM
#include "early_startup.inc"
#undef GRANTLEY_PRE_NEM

#if CONFIG(MICROCODE_UPDATE_PRE_RAM)
	/* Apply the matching update before enabling caches or entering NEM. */
	/* The assembly microcode loader returns through the address in ESP. */
	movl	$end_microcode_update, %esp
	jmp	update_bsp_microcode
end_microcode_update:
#endif

#define GRANTLEY_CACHE_PREP
#include "early_startup.inc"
#undef GRANTLEY_CACHE_PREP

	post_code(POSTCODE_SOC_SET_DEF_MTRR_TYPE)
	/* Clean-up MTRR_DEF_TYPE_MSR. */
	movl	$MTRR_DEF_TYPE_MSR, %ecx
	xorl	%eax, %eax
	xorl	%edx, %edx
	wrmsr

	post_code(POSTCODE_SOC_CLEAR_FIXED_MTRRS)
	/* Clear/disable fixed MTRRs */
	mov	$fixed_mtrr_list, %ebx
	xor	%eax, %eax
	xor	%edx, %edx

clear_fixed_mtrr:
	movzwl	(%ebx), %ecx
	wrmsr
	add	$2, %ebx
	cmp	$fixed_mtrr_list_end, %ebx
	jl	clear_fixed_mtrr

	/* Zero out all variable range MTRRs. */
	movl	$MTRR_CAP_MSR, %ecx
	rdmsr
	andl	$0xff, %eax
	shl	$1, %eax
	movl	%eax, %edi
	movl	$0x200, %ecx
	xorl	%eax, %eax
	xorl	%edx, %edx
clear_var_mtrrs:
	wrmsr
	add	$1, %ecx
	dec	%edi
	jnz	clear_var_mtrrs

	/* Determine CPU_ADDR_BITS and load PHYSMASK high word to %edx. */
	movl	$0x80000008, %eax
	cpuid
	movb	%al, %cl
	sub	$32, %cl
	movl	$1, %edx
	shl	%cl, %edx
	subl	$1, %edx

	/* Preload high word of address mask (in %edx) for Variable
	 * MTRRs 0 and 1.
	 */
addrsize_set_high:
	xorl	%eax, %eax
	movl	$MTRR_PHYS_MASK(0), %ecx
	wrmsr
	movl	$MTRR_PHYS_MASK(1), %ecx
	wrmsr

	post_code(POSTCODE_SOC_SET_MTRR_BASE)
	/* Set Cache-as-RAM base address. */
	movl	$(MTRR_PHYS_BASE(0)), %ecx
	movl	car_mtrr_start, %eax
	orl	$MTRR_TYPE_WRBACK, %eax
	xorl	%edx, %edx
	wrmsr

	post_code(POSTCODE_SOC_SET_MTRR_MASK)
	/* Set Cache-as-RAM mask. */
	movl	$(MTRR_PHYS_MASK(0)), %ecx
	rdmsr
	mov	car_mtrr_mask, %eax
	orl	$MTRR_PHYS_MASK_VALID, %eax
	wrmsr

	/* Enable cache for our code in Flash because we do XIP here */
	movl	$MTRR_PHYS_BASE(1), %ecx
	xorl	%edx, %edx
	mov	rom_mtrr_base, %eax
	orl	$MTRR_TYPE_WRPROT, %eax
	wrmsr

	movl	$MTRR_PHYS_MASK(1), %ecx
	rdmsr
	mov	rom_mtrr_mask, %eax
	orl	$MTRR_PHYS_MASK_VALID, %eax
	wrmsr

	post_code(POSTCODE_SOC_ENABLE_MTRRS)

	/* Enable MTRR. */
	movl	$MTRR_DEF_TYPE_MSR, %ecx
	rdmsr
	orl	$MTRR_DEF_TYPE_EN, %eax
	wrmsr

	/* Enable cache (CR0.CD = 0, CR0.NW = 0). */
	movl	%cr0, %eax
	andl	$(~(CR0_CacheDisable | CR0_NoWriteThrough)), %eax
	invd
	movl	%eax, %cr0

	/* Enable No-Eviction Mode - Setup State (MSR 0x2e0 bit 0) */
	movl	$NoEvictMod_MSR, %ecx
	rdmsr
	orl	$1, %eax
	andl	$~2, %eax
	wrmsr

	/* Address to touch cache line. */
	movl	$_car_mtrr_start, %edi

	/* Number of loops (64-byte cache lines). */
	movl	$_car_mtrr_size, %ecx
	shr	$6, %ecx
	xorl	%ebx, %ebx
	movb	$64, %bl

1:
	movl	%eax, (%edi)
	sfence
	addl	%ebx, %edi
	loop	1b

	/* Enable No-Eviction Mode - Run State (MSR 0x2e0 bit 1) */
	movl	$NoEvictMod_MSR, %ecx
	rdmsr
	orl	$2, %eax
	wrmsr

	post_code(POSTCODE_SOC_ENABLE_CACHE)

	/* Verify all CAR before using it for the stack or C data. */
	cld
	movl	$_car_mtrr_start, %edi
	movl	$_car_mtrr_size, %ecx
	shrl	$2, %ecx
	movl	$0x5a5a5a5a, %eax
	rep	stosl

	movl	$_car_mtrr_start, %edi
	movl	$_car_mtrr_size, %ecx
	shrl	$2, %ecx
1:
	cmpl	%eax, (%edi)
	jne	.Lcar_failure
	addl	$4, %edi
	loop	1b
	jmp	.Lcar_verified

.Lcar_failure:
	post_code(POSTCODE_RAM_FAILURE)
	hlt
	jmp	.Lcar_failure

.Lcar_verified:
	/* Now that CAR is enabled and verified clear it. */
	movl	$_car_mtrr_start, %edi
	movl	$_car_mtrr_size, %ecx
	cld
	xorl	%eax, %eax
	rep	stosb

	/* Set up the C stack, including padding for the three arguments. */
#define GRANTLEY_POST_NEM
#include "early_startup.inc"
#undef GRANTLEY_POST_NEM

	movl	$_ecar_stack, %esp
	andl	$0xfffffff0, %esp
	subl	$4, %esp

	/* Preserve the BIST result and initial TSC saved by entry32.S. */
	movd	%mm0, %eax
	pushl	%eax
	movd	%mm2, %eax
	pushl	%eax
	movd	%mm1, %eax
	pushl	%eax

#include <cpu/x86/copy_data_section.inc>

	post_code(POSTCODE_BOOTBLOCK_BEFORE_C_ENTRY)

	call	bootblock_c_entry_bist

.Lhlt:
	post_code(POSTCODE_DEAD_CODE)
	hlt
	jmp	.Lhlt

fixed_mtrr_list:
	.word	MTRR_FIX_64K_00000
	.word	MTRR_FIX_16K_80000
	.word	MTRR_FIX_16K_A0000
	.word	MTRR_FIX_4K_C0000
	.word	MTRR_FIX_4K_C8000
	.word	MTRR_FIX_4K_D0000
	.word	MTRR_FIX_4K_D8000
	.word	MTRR_FIX_4K_E0000
	.word	MTRR_FIX_4K_E8000
	.word	MTRR_FIX_4K_F0000
	.word	MTRR_FIX_4K_F8000
fixed_mtrr_list_end:

_cache_as_ram_setup_end:
