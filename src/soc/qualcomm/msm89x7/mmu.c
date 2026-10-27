/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/cache.h>
#include <arch/mmu.h>
#include <commonlib/helpers.h>
#include <console/console.h>
#include <device/mmio.h>
#include <soc/display_console.h>
#include <soc/mmu.h>
#include <soc/symbols.h>
#include <symbols.h>

void msm89x7_mmu_init(void)
{
	uintptr_t dt = read64((void *)_lk_dtb);
	uintptr_t cbmem_top = (uintptr_t)((uint64_t)CONFIG_MSM89X7_DDR_START +
					  (uint64_t)CONFIG_MSM89X7_DRAM_SIZE_MB * MiB);

	mmu_init();

	/* 1. System peripherals below DRAM (0 .. 1 GiB). */
	mmu_config_range((void *)0, (uintptr_t)_dram, MA_DEV | MA_RW);

	/* 2. coreboot execution region in DRAM (ROM, stack, TTB, stages, cache, payload).
	 * Fits in 74 MiB, well below the first firmware carveout at 0x84a00000. */
	mmu_config_range((void *)0x80000000, 0x04a00000, MA_MEM | MA_RW);

	/* 3. CBMEM allocation window at the top of physical DRAM. */
	mmu_config_range((void *)(cbmem_top - 2 * MiB), 2 * MiB, MA_MEM | MA_RW);

	/* 4. Handoff device tree blob from LK. */
	mmu_config_range((void *)ALIGN_DOWN(dt, 2 * MiB), 2 * MiB,
			 MA_MEM | MA_MEM_NC | MA_RW);

	/* Continuous splash framebuffer scanned out by MDP5. */
	if (CONFIG(MSM89X7_DISPLAY_CONSOLE)) {
		uint64_t base;
		size_t size;

		display_console_fb_region(&base, &size);
		if (size) {
			uint64_t end = ALIGN_UP(base + size, 4 * KiB);

			base = ALIGN_DOWN(base, 4 * KiB);
			mmu_config_range((void *)(uintptr_t)base, end - base,
					 MA_MEM | MA_MEM_NC | MA_RW);
		}
	}

	/* lk2nd ramoops console buffer. */
	mmu_config_range((void *)0xbff80000, 0x80000, MA_MEM | MA_MEM_NC | MA_RW);

	dcache_clean_invalidate_all();
	mmu_enable();
	printk(BIOS_INFO, "MSM89X7: MMU enabled\n");
}
