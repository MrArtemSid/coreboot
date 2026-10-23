/* SPDX-License-Identifier: GPL-2.0-only */
/* Private cold-boot BSP startup. No stack and no MM0/MM1/MM2 temporaries.
 * The single-socket ABI assigns all 256 ECAM buses to node zero, with IIO
 * on bus 00 and uncore on ff. Other topologies require a different contract.
 */
#ifndef GRANTLEY_STARTUP_MACROS
#define GRANTLEY_STARTUP_MACROS
#include <cpu/intel/grantley/grantley_routing.h>
#define GRANTLEY_UBOX 0x8ff87000
#define GRANTLEY_PCU 0x8fff2000

/* Legacy access: selector immediate, data EAX; EBX/ECX/ESI/EDI unchanged. */
.macro startup_pci_read selector
	movl	$\selector, %eax
	movw	$0xcf8, %dx
	outl	%eax, %dx
	movw	$0xcfc, %dx
	inl	%dx, %eax
.endm

/* Verify all writable bits requested by mask, with expected data in EBX. */
.macro startup_pci_write selector, mask=0xffffffff
	movl	$\selector, %eax
	movw	$0xcf8, %dx
	outl	%eax, %dx
	movl	%ebx, %eax
	movw	$0xcfc, %dx
	outl	%eax, %dx
	inl	%dx, %eax
	xorl	%ebx, %eax
	testl	$\mask, %eax
	jnz	.Lstartup_failure
.endm

.macro startup_mmio_verify address, expected, mask
	movl	\address, %eax
	andl	$\mask, %eax
	cmpl	$\expected, %eax
	jne	.Lstartup_failure
.endm
#endif

#ifdef GRANTLEY_PRE_NEM
	/* Reject a different CPU family or node layout before using its CSRs. */
	movl	$1, %eax
	cpuid
	movl	%eax, %esi
	andl	$0xffff0, %eax
	cmpl	$0x306f0, %eax
	je	.Lstartup_family_ok
	cmpl	$0x406f0, %eax
	jne	.Lstartup_failure
.Lstartup_family_ok:
	startup_pci_read 0x80017df4
	testl	$0xb, %eax
	jnz	.Lstartup_failure
	startup_pci_read 0x80017df0
	testl	$0x38, %eax
	jnz	.Lstartup_failure

	/* CPUBUSNO changes the bus used by subsequent CF8 transactions.
	 * Its readback must select the NEW bus, not the obsolete reset alias.
	 */
	startup_pci_read 0x80018700
	cmpw	$0x8086, %ax
	jne	.Lstartup_failure
	movl	$0x800187d0, %eax
	movw	$0xcf8, %dx
	outl	%eax, %dx
	movl	$GRANTLEY_CPUBUSNO, %eax
	movw	$0xcfc, %dx
	outl	%eax, %dx
	startup_pci_read 0x80ff87d0
	cmpl	$GRANTLEY_CPUBUSNO, %eax
	jne	.Lstartup_failure
	startup_pci_read 0x80ff8700
	cmpw	$0x8086, %ax
	jne	.Lstartup_failure
	startup_pci_read 0x80ff7d00
	cmpw	$0x8086, %ax
	jne	.Lstartup_failure
	startup_pci_read 0x80002800
	cmpw	$0x8086, %ax
	jne	.Lstartup_failure

	/* Each 32-bus target field is zero; length 00 encodes 256 buses. */
	xorl	%ebx, %ebx
	startup_pci_write 0x80ff7de4, 0xffffff
	movl	$(GRANTLEY_ECAM_BASE | 1), %ebx
	startup_pci_write 0x80ff7dc0, 0xfc000007
	startup_pci_read 0x80ff87dc
	andl	$~7, %eax
	orl	$1, %eax
	movl	%eax, %ebx
	startup_pci_write 0x80ff87dc, 7

	/* A0 has 32-bit base/limit at 84/88. Later silicon has 64-bit
	 * fields at 90/98: clear BOTH high words before enabling CSR use.
	 * Limit stores address bits 31:26, so 8c covers through 8fffffff.
	 */
	cmpl	$0x306f0, %esi
	je	.Lstartup_iio_a0
	xorl	%ebx, %ebx
	startup_pci_write 0x80002894
	startup_pci_write 0x8000289c
	movl	$GRANTLEY_ECAM_BASE, %ebx
	startup_pci_write 0x80002890, 0xfc000000
	movl	$GRANTLEY_ECAM_LIMIT, %ebx
	startup_pci_write 0x80002898, 0xfc000000
	jmp	.Lstartup_iio_ready
.Lstartup_iio_a0:
	movl	$GRANTLEY_ECAM_BASE, %ebx
	startup_pci_write 0x80002884, 0xfc000000
	movl	$GRANTLEY_ECAM_LIMIT, %ebx
	startup_pci_write 0x80002888, 0xfc000000
.Lstartup_iio_ready:
	movl	0x8ff85040, %eax
	andl	$~7, %eax
	movl	%eax, 0x8ff85040
	startup_mmio_verify 0x8ff85040, 0, 7

	/* MY_NID=0 and AM_I_MASTER=1. MASTER_NID retains its cold-reset
	 * node-zero value; do not alter unrelated package C-state controls.
	 */
	movl	(GRANTLEY_PCU + 0x6c), %eax
	andl	$~0x700, %eax
	orl	$4, %eax
	movl	%eax, (GRANTLEY_PCU + 0x6c)
	startup_mmio_verify (GRANTLEY_PCU + 0x6c), 4, 0x704

	startup_mmio_verify GRANTLEY_PCU, 0x8086, 0xffff
	movl	(GRANTLEY_PCU + 0x40), %eax
	andl	$0xffff0000, %eax
	orl	$GRANTLEY_CPUBUSNO, %eax
	movl	%eax, (GRANTLEY_PCU + 0x40)
	startup_mmio_verify (GRANTLEY_PCU + 0x40), GRANTLEY_CPUBUSNO, 0x8000ffff
	movl	0x80028108, %eax
	andl	$0xfffe0000, %eax
	orl	$GRANTLEY_IIO_BUS_RULE, %eax
	movl	%eax, 0x80028108
	startup_mmio_verify 0x80028108, GRANTLEY_IIO_BUS_RULE, 0x1ffff

	jmp	.Lstartup_route_ready
.Lstartup_failure:
	/* A routing prerequisite failed: never attempt cache/Uncore access. */
	post_code(0xcc)
	cli
.Lstartup_failed_halt:
	hlt
	jmp	.Lstartup_failed_halt
.Lstartup_route_ready:
#endif

#ifdef GRANTLEY_CACHE_PREP
	/* Architectural state may survive a soft reset. Clear CPUID limiting
	 * before topology discovery, and canonicalize only APIC address bits;
	 * preserve BSP, enable and x2APIC mode rather than force a mode switch.
	 */
	movl	$0x1a0, %ecx
	rdmsr
	btrl	$22, %eax
	jnc	.Lstartup_cpuid_unlimited
	wrmsr
.Lstartup_cpuid_unlimited:
	movl	$0x1b, %ecx
	rdmsr
	andl	$0x00000fff, %eax
	orl	$0xfee00000, %eax
	andl	$~0xf, %edx
	wrmsr

	/* Cold boot has no request for 16 KiB ECC DCU mode or enabling the
	 * debug interface. Do not access MSR 31 solely because PLATFORM_INFO
	 * advertises its capability. Sticky policy replay and the PCU A4
	 * core/ring command guarded by completed cold-reset state belong to
	 * a separate warm-reset path, not this cold-only entry.
	 */
	/* VCU real-hardware flag is the low byte only. Simulation has no
	 * mailbox contract. Each wait, including the initial idle wait, is
	 * bounded independently. A VCU failure is diagnostic, as in the stock
	 * cold-boot path; it must not introduce a new unconditional boot stop.
	 */
	cmpb	$0, 0x8ff850b0
	jne	.Lstartup_ready
.macro startup_vcu_wait
	movl	$0x1000, %ecx
.Lvcu_poll\@:
	movl	0x8fff8040, %eax
	cmpl	$0xffffffff, %eax
	je	.Lvcu_failure
	testl	$0x80000000, %eax
	jz	.Lvcu_idle\@
	pause
	decl	%ecx
	jnz	.Lvcu_poll\@
	jmp	.Lvcu_failure
.Lvcu_idle\@:
.endm
	startup_vcu_wait
	movl	$0x1000a, 0x8fff8044
	movl	$0x80000002, 0x8fff8040
	startup_vcu_wait
	movl	$0, 0x8fff8044
	movl	$0x80000006, 0x8fff8040
	startup_vcu_wait
	movl	$0x8001100f, 0x8fff8040
	startup_vcu_wait
	movl	$0x1000a, 0x8fff8044
	movl	$0x80000003, 0x8fff8040
	startup_vcu_wait
	jmp	.Lstartup_ready
.Lvcu_failure:
	post_code(0xcb)
.Lstartup_ready:
#endif

#ifdef GRANTLEY_POST_NEM
	/* The PROCHOT workaround is specific to Haswell-EP A0. Never infer
	 * engineering-sample register compatibility from family ordering.
	 */
	movl	$1, %eax
	cpuid
	cmpl	$0x306f0, %eax
	jne	.Lstartup_prochot_ready
	movl	$0x1fc, %ecx
	rdmsr
	orl	$((1 << 21) | 1), %eax
	wrmsr
.Lstartup_prochot_ready:
	/* TXT memory unlock is permitted only with SMX and this package's
	 * TXT status bit asserted. An absent MMIO register is not permission.
	 * The TPM status read must precede the memory-unlock MSR write.
	 */
	movl	$1, %eax
	cpuid
	testl	$(1 << 6), %ecx
	jz	.Lstartup_txt_ready
	movl	0xfed30010, %eax
	cmpl	$0xffffffff, %eax
	je	.Lstartup_txt_ready
	testl	$1, %eax
	jz	.Lstartup_txt_ready
	movl	0xfed40000, %eax
	movl	$0x2e6, %ecx
	xorl	%eax, %eax
	xorl	%edx, %edx
	wrmsr
.Lstartup_txt_ready:
	/* Publish the package BSP ABI consumed by Uncore: check-in bit 0,
	 * OC PLL ratio [3:2], initial APIC ID [23:8], stepping [30:27].
	 * All other scratch fields survive. No OC-capability means MSR 61e
	 * is not readable, not merely that its result should be ignored.
	 */
	movl	$1, %eax
	cpuid
	andl	$15, %eax
	shll	$27, %eax
	movl	%eax, %esi
	shrl	$24, %ebx
	shll	$8, %ebx
	movl	%ebx, %edi
	xorl	%eax, %eax
	cpuid
	cmpl	$0xb, %eax
	jb	.Lstartup_apic_id_ready
	movl	$0xb, %eax
	xorl	%ecx, %ecx
	cpuid
	testl	%ebx, %ebx
	jz	.Lstartup_apic_id_ready
	movzwl	%dx, %edi
	shll	$8, %edi
.Lstartup_apic_id_ready:
	orl	%edi, %esi
	xorl	%edi, %edi
	movl	0x8fff3094, %eax
	cmpl	$0xffffffff, %eax
	je	.Lstartup_post_failure
	testl	$(1 << 28), %eax
	jz	.Lstartup_pll_ready
	movl	$0x61e, %ecx
	rdmsr
	andl	$3, %eax
	shll	$2, %eax
	movl	%eax, %edi
.Lstartup_pll_ready:
	movl	(GRANTLEY_UBOX + 0x68), %eax
	andl	$0x870000f3, %eax
	orl	%esi, %eax
	orl	%edi, %eax
	orl	$1, %eax
	movl	%eax, (GRANTLEY_UBOX + 0x68)
	cmpl	(GRANTLEY_UBOX + 0x68), %eax
	jne	.Lstartup_post_failure
	jmp	.Lstartup_post_ready
.Lstartup_post_failure:
	post_code(0xcc)
	cli
.Lstartup_post_halt:
	hlt
	jmp	.Lstartup_post_halt
.Lstartup_post_ready:
#endif
