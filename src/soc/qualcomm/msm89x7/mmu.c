/* SPDX-License-Identifier: GPL-2.0-only */

#include <symbols.h>
#include <arch/mmu.h>
#include <arch/cache.h>
#include <soc/mmu.h>
#include <soc/addressmap.h>

void msm89x7_mmu_init(void)
{
	mmu_init();

	/* Everything below DRAM is device memory (peripherals, IMEM). */
	mmu_config_range((void *)0, (uintptr_t)_dram, MA_DEV | MA_RW);
	mmu_config_range(_dram, (uintptr_t)CONFIG_MSM89X7_DRAM_SIZE_MB * MiB,
			 MA_MEM | MA_S | MA_RW);

	/* SMEM is shared with other masters, keep it uncached. */
	mmu_config_range((void *)SMEM_BASE, SMEM_SIZE, MA_DEV | MA_RW);

	mmu_enable();
}
