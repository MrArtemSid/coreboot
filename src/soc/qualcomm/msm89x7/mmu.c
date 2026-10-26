/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/mmio.h>
#include <symbols.h>
#include <arch/mmu.h>
#include <arch/cache.h>
#include <arch/hlt.h>
#include <arch/lib_helpers.h>
#include <soc/mmu.h>
#include <soc/addressmap.h>
#include <soc/ramoops.h>
#include <soc/display_console.h>
#include <console/console.h>
#include <console/ramoops_console.h>

void msm89x7_mmu_init(void)
{
	mmu_init();
	mmu_config_range((void *)0, (uintptr_t)_dram, MA_DEV | MA_RW);
	mmu_config_range(_dram, (uintptr_t)CONFIG_MSM89X7_DRAM_SIZE_MB * MiB,
			 MA_MEM | MA_RW);
	mmu_config_range((void *)SMEM_BASE, SMEM_SIZE, MA_DEV | MA_RW);
	if (CONFIG(MSM89X7_DISPLAY_CONSOLE)) {
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
	mmu_config_range((void *)(uintptr_t)0xbff80000, 0x80000,
			 MA_MEM | MA_MEM_NC | MA_RW);
	/* Early-fault beacons: raw ramoops bytes survive even without a
	 * working console and mark exactly which enable step died. */
	__ramoops_tx_byte('1');
	dcache_clean_invalidate_all();
	__ramoops_tx_byte('2');
	mmu_enable();
	__ramoops_tx_byte('3');
	printk(BIOS_INFO, "MSM89X7: MMU enabled\n");
}
