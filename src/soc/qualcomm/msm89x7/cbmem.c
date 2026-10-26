/* SPDX-License-Identifier: GPL-2.0-only */

#include <cbmem.h>
#include <commonlib/helpers.h>
#include <console/console.h>
#include <console/ramoops_console.h>
#include <arch/stages.h>
#include <program_loading.h>

#include <soc/ramoops.h>

uintptr_t cbmem_top_chipset(void)
{
	return (uintptr_t)((uint64_t)CONFIG_MSM89X7_DDR_START +
			   (uint64_t)CONFIG_MSM89X7_DRAM_SIZE_MB * MiB);
}

void platform_romstage_main(void)
{
	__ramoops_tx_byte('R');
	__ramoops_tx_byte('M');
	__ramoops_tx_byte('\n');
	if (CONFIG(MSM89X7_RAMOOPS_CONSOLE))
		msm89x7_wdt_arm(3000);
	printk(BIOS_INFO, "=== MSM89X7 ROMSTAGE STARTED ===\n");
}

void platform_prog_run(struct prog *prog)
{
	__ramoops_tx_byte('P');
	__ramoops_tx_byte('R');
	__ramoops_tx_byte(':');
	const char *name = prog_name(prog);
	while (*name)
		__ramoops_tx_byte(*name++);
	__ramoops_tx_byte('\n');

	/* Keep diagnostic resets enabled through stage loading, not in the payload. */
	if (ENV_RAMSTAGE && prog_type(prog) == PROG_PAYLOAD &&
	    CONFIG(MSM89X7_RAMOOPS_CONSOLE))
		msm89x7_wdt_disable();
}
