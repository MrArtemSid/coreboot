/* SPDX-License-Identifier: GPL-2.0-only */

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
