/* SPDX-License-Identifier: GPL-2.0-only */

#define __SIMPLE_DEVICE__

#include <arch/romstage.h>
#include <cbmem.h>
#include <commonlib/helpers.h>
#include <cpu/x86/mtrr.h>
#include <cpu/x86/smm.h>
#include <stdint.h>
#include <types.h>
#include "uncore_runner/pei_services.h"

/*
 * Default Top of Low Usable Memory (TOLUM) fallback for Haswell-EP.
 * Under 4 GiB, MMIO space (PCIe, MMCFG, APIC) is typically mapped at 0x80000000..0xffffffff.
 * Usable low RAM sits below TOLUM (or 0x80000000 / 2 GiB minimum).
 */
#define GRANTLEY_DEFAULT_TOLUM  0x80000000UL

static uintptr_t top_of_low_usable_memory(void)
{
	const struct grantley_uncore_state *state = grantley_get_uncore_state();

	if (state && state->dram_initialized && state->dram_size > 0) {
		/*
		 * If total RAM is below 2 GiB, top is dram_size aligned down to 8 MiB.
		 * Otherwise low RAM ceiling below MMIO hole is 0x80000000 (2 GiB)
		 * minus SMM TSEG.
		 */
		uintptr_t low_ram_top = (state->dram_size < GRANTLEY_DEFAULT_TOLUM) ?
			(uintptr_t)state->dram_size : GRANTLEY_DEFAULT_TOLUM;

		if (CONFIG(SMM_TSEG) && CONFIG_SMM_TSEG_SIZE > 0)
			low_ram_top -= CONFIG_SMM_TSEG_SIZE;

		return ALIGN_DOWN(low_ram_top, 8 * MiB);
	}

	return GRANTLEY_DEFAULT_TOLUM - (CONFIG(SMM_TSEG) ? CONFIG_SMM_TSEG_SIZE : 0);
}

uintptr_t cbmem_top_chipset(void)
{
	return top_of_low_usable_memory();
}

void smm_region(uintptr_t *start, size_t *size)
{
	*size = CONFIG_SMM_TSEG_SIZE;
	*start = cbmem_top_chipset();
}

void fill_postcar_frame(struct postcar_frame *pcf)
{
	/* Cache at least 8 MiB below top of RAM for CBMEM and ramstage execution */
	const uintptr_t top_of_ram = ALIGN_DOWN(cbmem_top(), 8 * MiB);

	postcar_frame_add_mtrr(pcf, top_of_ram - 8 * MiB, 16 * MiB, MTRR_TYPE_WRBACK);
}
