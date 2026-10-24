/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Bridge between coreboot SoC code and the SCR-HAL platform headers.
 *
 * This translation unit is compiled with SCR-HAL flags only (see
 * libscrhal in Makefile.mk) and never includes coreboot headers: the
 * two header families define overlapping RISC-V standard macro names
 * with different spellings. Expose plain functions and addresses.
 */

#include <stdint.h>
#include <stddef.h>

#include "drivers/rtc.h"
#include "drivers/mtimer.h"
#include "drivers/pmp.h"
#include "drivers/pma.h"
#include "drivers/mrt.h"
#include "plf.h"

#define SCR_REG32(addr)		(*(volatile uint32_t *)(uintptr_t)(addr))

void scr_hal_mtimer_enable(void)
{
	/* Enable the machine timer: platform clock source, running. */
	SCR_REG32(SCR_RTC_CTL) = SCR_MTIMER_CTRL_EN | SCR_PLF_RTC_SRC;
	SCR_REG32(SCR_RTC_DIVIDER) = SCR_PLF_TIMEBASE_DIV;
}

uintptr_t scr_hal_mtimer_time_addr(void)
{
	return (uintptr_t)SCR_RTC_MTIME;
}

uintptr_t scr_hal_mtimer_cmp_addr(unsigned long hart)
{
	return (uintptr_t)SCR_RTC_MTIMECMP_CORE(hart);
}

uint64_t scr_hal_mtimer_now(void)
{
	uint32_t hi, lo;

	do {
		hi = SCR_REG32(SCR_RTC_MTIMEH);
		lo = SCR_REG32(SCR_RTC_MTIME);
	} while (SCR_REG32(SCR_RTC_MTIMEH) != hi);

	return ((uint64_t)hi << 32) | lo;
}

unsigned long scr_hal_timebase_hz(void)
{
	return PLF_RTC_TIMEBASE;
}

uintptr_t scr_hal_uart_base(void)
{
	return (uintptr_t)PLF_UART0_BASE;
}

#ifdef PLF_IMSIC_BASE_M
/*
 * IPI delivery through the AIA/IMSIC gateway, miniboot-style: write the
 * target hart's machine seteipnum_le word so the gateway raises an MSI.
 */
int scr_hal_imsic_send_ipi(unsigned long hart)
{
	SCR_REG32(IMSIC_BASE_ADDR(IMSIC_M, hart) + IMSIC_SETEIPNUM_LE * 4) = 1;
	return 1;
}
#else
int scr_hal_imsic_send_ipi(unsigned long hart)
{
	(void)hart;
	return 0; /* board has no AIA/IMSIC gateway */
}
#endif

/*
 * MRT (cluster Memory Region Table) programming, miniboot-style: the
 * cluster config space (MRT table itself, ICCM mailboxes, cluster
 * mtimer, PLIC) is only reachable through regions mapped with the
 * CLUSTER_CSR_MPORT attribute. scr-hal's canonical mrt_init() walks
 * the platform PLF_MEM_MAP, which already carries the right per-region
 * MPORT attributes; it just needs the mem_regions[] table that normally
 * lives in scr-hal's sys_init.c (not linked here).
 */
const scr_mem_region_info mem_regions[] = { PLF_MEM_MAP };
const unsigned mem_region_count = sizeof(mem_regions) / sizeof(mem_regions[0]);

void scr_hal_mrt_init(void)
{
#if defined(PLF_MRT_SUPPORT) && (PLF_MRT_SUPPORT == 1)
	mrt_init();
#endif
}

/*
 * Memory typing for the platform regions, miniboot-style: the SCR
 * cluster config / MMCFG space (L2/L3 cache controllers) faults unless
 * a PMP entry (or PMA region) types it as config space, so the exact
 * miniboot layout is installed before any payload takes over:
 *   entry 13: OCRAM weakly-ordered/cacheable
 *   entry 14: MMCFG (cluster config) as config space
 *   entry 15: DDR weakly-ordered/cacheable
 * On cores with PMA CSRs the equivalent scr-hal PMA regions are used
 * instead, exactly like miniboot's pmp_early_init/pmp_init pair.
 */
void scr_hal_pmp_platform_init(void)
{
	if (pma_is_available()) {
		size_t pma_num = get_pma_regions();

		set_pma_region(pma_num - 3, PLF_OCRAM_BASE, PLF_OCRAM_SIZE,
			       PMA_MEM_REGION_C_WO);
		set_pma_region(pma_num - 2, PLF_MMCFG_BASE, PLF_MMCFG_SIZE,
			       PMA_MEM_REGION_IO);
		set_pma_region(pma_num - 1, PLF_MEM_BASE, PLF_MEM_SIZE,
			       PMA_MEM_REGION_C_WO);
		return;
	}

	create_pmp_region(13, PLF_OCRAM_BASE, ~(PLF_OCRAM_SIZE - 1),
			  PLF_OCRAM_ATTR);
	create_pmp_region(14, PLF_MMCFG_BASE, ~(PLF_MMCFG_SIZE - 1),
			  PLF_MMCFG_ATTR);
	create_pmp_region(15, PLF_MEM_BASE, ~(PLF_MEM_SIZE - 1),
			  PLF_MEM_ATTR);
	ifence();
}
