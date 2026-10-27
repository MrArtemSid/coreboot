/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/lib_helpers.h>
#include <bootblock_common.h>
#include <console/console.h>
#include <soc/mmu.h>
#include <soc/ramoops.h>

/* Hidden visibility forces a direct PC-relative reference on AArch64,
 * so this reports where LK actually loaded us, not a pointer from the GOT. */
extern const unsigned char _start[] __attribute__((visibility("hidden")));

void bootblock_soc_early_init(void)
{
	/* Catch hangs from here on, including the MMU setup. */
	msm89x7_wdt_arm(3000);
}

void bootblock_soc_init(void)
{
	printk(BIOS_INFO, "MSM89X7: EL=%llu start=0x%llx\n",
	       (unsigned long long)(raw_read_currentel() >> 2),
	       (unsigned long long)(uintptr_t)_start);
	msm89x7_mmu_init();
}
