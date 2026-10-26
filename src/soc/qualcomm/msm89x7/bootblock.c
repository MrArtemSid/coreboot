/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/exception.h>
#include <arch/hlt.h>
#include <arch/lib_helpers.h>
#include <bootblock_common.h>
#include <device/mmio.h>
#include <soc/ramoops.h>
#include <console/console.h>
#include <console/display_console.h>
#include <console/ramoops_console.h>
#include <program_loading.h>
#include <soc/mmu.h>

/* Temporary first-fault recorder for MMU bring-up. Do not use printk or
 * dump the interrupted stack: either can fault again and hide the cause. */
static bool mmu_exception_active;
static struct exception_handler mmu_exception_handlers[NUM_EXC_VIDS];

/* Hidden visibility forces a direct PC-relative reference on AArch64,
 * so this reports where LK actually loaded us, not a pointer from the GOT. */
extern const unsigned char _start[] __attribute__((visibility("hidden")));

static void mmu_diag_byte(unsigned char byte)
{
	/* Persist first: a framebuffer access may itself be the fault. */
	__ramoops_tx_byte(byte);
	__display_console_tx_byte(byte);
}

void mmu_diag_text(const char *text)
{
	while (*text)
		mmu_diag_byte(*text++);
}

static void mmu_diag_hex(const char *name, uint64_t value)
{
	static const char hex[] = "0123456789abcdef";

	mmu_diag_text(name);
	mmu_diag_text("=0x");
	for (int shift = 60; shift >= 0; shift -= 4)
		mmu_diag_byte(hex[(value >> shift) & 0xf]);
	mmu_diag_byte('\n');
}

static int mmu_first_exception(struct exc_state *state, uint64_t vector_id)
{
	if (mmu_exception_active)
		hlt();
	mmu_exception_active = true;

	/* Snapshot the fault before doing any framebuffer accesses. */
	uint64_t esr = raw_read_esr();
	uint64_t far = raw_read_far();
	uint64_t el = raw_read_currentel() >> 2;
	uint64_t sctlr = raw_read_sctlr();
	uint64_t elr = state->elx.elr;
	uint64_t spsr = state->elx.spsr;
	uint64_t sp = state->regs.sp;

	/* Reuse the live framebuffer; reinitializing MDP here can fault
	 * before the syndrome reaches either console. */
	mmu_diag_text("CB19: MMU FIRST EXCEPTION\n");
	mmu_diag_hex("VECTOR", vector_id);
	mmu_diag_hex("EL", el);
	mmu_diag_hex("ESR", esr);
	mmu_diag_hex("ELR", elr);
	mmu_diag_hex("FAR", far);
	mmu_diag_hex("SPSR", spsr);
	mmu_diag_hex("SCTLR", sctlr);
	mmu_diag_hex("SP", sp);
	mmu_diag_text("HALTED - photograph this screen\n");
	hlt();
	return EXC_RET_ABORT;
}

void msm89x7_mmu_probe_fault(uint64_t vector, uint64_t esr, uint64_t far,
			   uint64_t elr, uint64_t sctlr)
{
	/* The assembly vector has already disabled M/C and reset the stack.
	 * Reuse the live framebuffer; do not touch MDP or clear the screen. */
	mmu_diag_text("\nMMU-DIAG14: STACKLESS PROBE FAULT; MMU OFF\n");
	mmu_diag_hex("VECTOR", vector);
	mmu_diag_hex("ESR", esr);
	mmu_diag_hex("FAR", far);
	mmu_diag_hex("ELR", elr);
	mmu_diag_hex("FAULT_SCTLR", sctlr);
	mmu_diag_text("HALTED - photograph this screen\n");
	hlt();
	__builtin_unreachable();
}

void bootblock_soc_early_init(void)
{
	/* Bring-up watchdog: catch ANY hang from this point on (pre-MMU,
	 * console, MMU enable) and return the board to lk2nd fastboot,
	 * where the ramoops log can be fetched. Console/ramoops bytes pet
	 * it; a silent hang bites after 3 s. */
	if (CONFIG(MSM89X7_RAMOOPS_CONSOLE)) {
		write32((void *)(0x0b017000 + 0x08), 0);
		msm89x7_wdt_arm(3000);
	}
}

/* Intercept stage transitions to trace execution before jumping */
void platform_prog_run(struct prog *prog)
{
	printk(BIOS_INFO, "[BOOTBLOCK] Jumping to %s at %p...\n",
	       prog_name(prog), prog_entry(prog));
}

void bootblock_soc_init(void)
{
	__ramoops_tx_byte('B');
	__ramoops_tx_byte('B');
	__ramoops_tx_byte('\n');
	printk(BIOS_NOTICE, "CB19: C ENTRY OK; CONTINUING TO MMU\n");

	mmu_exception_active = false;
	for (size_t i = 0; i < NUM_EXC_VIDS; ++i) {
		mmu_exception_handlers[i].handler = mmu_first_exception;
		exception_handler_register(i, &mmu_exception_handlers[i]);
	}

	printk(BIOS_INFO, "MSM89X7: EL=%llu start=0x%llx\n",
	       (unsigned long long)(raw_read_currentel() >> 2),
	       (unsigned long long)(uintptr_t)_start);

	if ((uintptr_t)_start != CONFIG_MSM89X7_BOOT_IMAGE_ADDR) {
		printk(BIOS_ERR, "LOAD ADDRESS MISMATCH - HALTED\n");
		hlt();
	}

	/* Initialize MMU, enable I-Cache and D-Cache */
	msm89x7_mmu_init();

	/* Catch an endless loop in later stages: after 3 s without a
	 * bite the watchdog resets the board and lk2nd serves the
	 * ramoops log over fastboot. A die() re-arms it short (see
	 * die_notify()). */
	__ramoops_tx_byte('4');
	if (CONFIG(MSM89X7_RAMOOPS_CONSOLE))
		msm89x7_wdt_arm(3000);
	__ramoops_tx_byte('5');

	printk(BIOS_INFO, "MSM89X7: bootblock complete, jumping to romstage...\n");
	__ramoops_tx_byte('6');
}
