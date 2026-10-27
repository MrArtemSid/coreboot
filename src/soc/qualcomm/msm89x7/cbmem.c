/* SPDX-License-Identifier: GPL-2.0-only */

#include <cbmem.h>
#include <commonlib/helpers.h>
#include <program_loading.h>
#include <soc/ramoops.h>

uintptr_t cbmem_top_chipset(void)
{
	return (uintptr_t)((uint64_t)CONFIG_MSM89X7_DDR_START +
			   (uint64_t)CONFIG_MSM89X7_DRAM_SIZE_MB * MiB);
}

void platform_prog_run(struct prog *prog)
{
	/* The payload owns the watchdog. */
	if (ENV_RAMSTAGE && prog_type(prog) == PROG_PAYLOAD)
		msm89x7_wdt_disable();
}
