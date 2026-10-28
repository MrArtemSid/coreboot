/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <drivers/cache.h>
#include <drivers/pmp.h>
#include <drivers/pma.h>
#include <drivers/mrt.h>
#include <drivers/aia.h>
#include <drivers/rtc.h>
#include <perf.h>

_Static_assert(CONFIG_SCR_OCRAM_SIZE <= PLF_OCRAM_SIZE, "OCRAM exceeds HAL platform");

void plf_init_features(void);

void scr_hal_early_init(void)
{
	perf_init_hpm();
	if (pma_is_available()) {
		set_pma_region(get_pma_regions() - 2, PLF_MMCFG_BASE, PLF_MMCFG_SIZE,
			       PMA_MEM_REGION_IO);
	} else {
		create_pmp_region(14, PLF_MMCFG_BASE, ~(PLF_MMCFG_SIZE - 1),
				  PLF_MMCFG_ATTR);
	}
	ifence();
}

void scr_hal_cache_shared_init(void)
{
	/* Like miniboot: invalidate all L3 banks before enabling the inner caches. */
	plf_l3cache_init();
#if !defined(PLF_CACHE_L2_DEDICATED) || !PLF_CACHE_L2_DEDICATED
	/* HAL L2 init invalidates without cleaning. Never reinitialize a live L2. */
	if (!plf_l2cache_is_enabled())
		plf_l2cache_init();
#endif
	fence();
}

void scr_hal_cache_local_init(void)
{
#if defined(PLF_CACHE_L2_DEDICATED) && PLF_CACHE_L2_DEDICATED
	if (!plf_l2cache_is_enabled())
		plf_l2cache_init();
#endif
	plf_l1cache_init();
	ifence();
}

void scr_hal_memory_init(void)
{
	if (pma_is_available()) {
		size_t n = get_pma_regions();

		set_pma_region(n - 3, PLF_OCRAM_BASE, CONFIG_SCR_OCRAM_SIZE,
			       PMA_MEM_REGION_C_WO);
		set_pma_region(n - 2, PLF_MEM_BASE, PLF_MEM_SIZE, PMA_MEM_REGION_C_WO);
		set_pma_region(n - 1, 0, 0, PMA_MEM_REGION_IO);
	} else {
		create_pmp_region(13, PLF_OCRAM_BASE, ~(CONFIG_SCR_OCRAM_SIZE - 1UL),
				  PLF_OCRAM_ATTR);
		create_pmp_region(15, PLF_MEM_BASE, ~(PLF_MEM_SIZE - 1), PLF_MEM_ATTR);
	}
	ifence();
}

void scr_hal_features_init(void)
{
	plf_init_features();
}

void scr_hal_timer_init(void)
{
	unsigned long clock = PLF_SYS_CLK;

#if PLF_RTC_SRC_EXTERNAL
	clock /= 4;
#endif
	/* Match rtc_init(), using the FPGA clock without the HAL hwinfo runtime. */
	SCR_MTIMER.ctrl = 0;
	SCR_MTIMER.div = clock / PLF_RTC_TIMEBASE - 1;
	SCR_MTIMER.time = 0;
	SCR_MTIMER.cmp = ~(uint64_t)0;
	scr_mtimer_enable();
}

void scr_hal_mrt_init(void)
{
#if defined(PLF_MRT_SUPPORT) && (PLF_MRT_SUPPORT == 1)
	const scr_mem_region_info mrt_regions[] = {
		/* A zero size covers the entire address space. */
		{0, 0, 0, NULL, PLF_DEFAULT_MPORT},
		{PLF_CLUSTER_CFG_BASE, 0x4000000UL, 0, NULL, CLUSTER_CSR_MPORT},
#if defined(PLF_IMSIC_BASE_M) && defined(PLF_IMSIC_BASE_S)
		{PLF_IMSIC_BASE_M, PLF_IMSIC_SIZE_M, 0, NULL, CLUSTER_CSR_MPORT},
		{PLF_IMSIC_BASE_S, PLF_IMSIC_SIZE_S, 0, NULL, CLUSTER_CSR_MPORT},
#else
		/* Temporary AIA gateway mapping, matching miniboot. */
		{0x10000000UL, 0x4000000UL, 0, NULL, CLUSTER_CSR_MPORT},
#endif
	};
	unsigned int region_count = sizeof(mrt_regions) / sizeof(*mrt_regions);

	if (!aia_is_available())
		region_count--;

	setup_mrt_regions(mrt_regions, region_count);
#endif
}

int scr_hal_mrt_is_initialized(void)
{
#if defined(PLF_MRT_SUPPORT) && (PLF_MRT_SUPPORT == 1)
	/* Miniboot's active entry 1 covers all addresses: base and mask are zero. */
	volatile size_t *mrt_entry_ctrl = (volatile size_t *)MRT_A_ENTRY_N_CTRL(MRT_ENTRY_START);
	volatile size_t *mrt_entry_addr = (volatile size_t *)MRT_A_ENTRY_N_ADDR(MRT_ENTRY_START);
	volatile size_t *mrt_entry_mask = (volatile size_t *)MRT_A_ENTRY_N_MASK(MRT_ENTRY_START);

	return (*mrt_entry_ctrl & MRT_ENTRY_VALID) &&
		*mrt_entry_addr == 0 && *mrt_entry_mask == 0;
#else
	return 1;
#endif
}
