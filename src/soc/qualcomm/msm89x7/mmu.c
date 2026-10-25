/* SPDX-License-Identifier: GPL-2.0-only */

#include <symbols.h>
#include <arch/mmu.h>
#include <arch/cache.h>
#include <soc/mmu.h>
#include <soc/addressmap.h>
#include <soc/display_console.h>

void msm89x7_mmu_init(void)
{
	mmu_init();

	/* Everything below DRAM is device memory (peripherals, IMEM). */
	mmu_config_range((void *)0, (uintptr_t)_dram, MA_DEV | MA_RW);
	mmu_config_range(_dram, (uintptr_t)CONFIG_MSM89X7_DRAM_SIZE_MB * MiB,
			 MA_MEM | MA_S | MA_RW);

	/* SMEM is shared with other masters, keep it uncached. */
	mmu_config_range((void *)SMEM_BASE, SMEM_SIZE, MA_DEV | MA_RW);

	if (CONFIG(MSM89X7_DISPLAY_CONSOLE)) {
		/* The splash framebuffer is scanned out by the MDP, keep
		 * it uncached so drawing text becomes visible right
		 * away. Use the region detected from the live MDP
		 * configuration; fall back to the Kconfig geometry. */
		uint64_t base;
		size_t size;

		display_console_fb_region(&base, &size);
		if (!size) {
			base = CONFIG_MSM89X7_FB_ADDR;
			size = (size_t)CONFIG_MSM89X7_FB_WIDTH *
			       CONFIG_MSM89X7_FB_HEIGHT * 4;
		}
		mmu_config_range((void *)(uintptr_t)base, size,
				 MA_MEM | MA_MEM_NC | MA_RW);
	}

	mmu_enable();
}
