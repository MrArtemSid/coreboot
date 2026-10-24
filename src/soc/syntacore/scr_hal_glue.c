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
#include "drivers/iccm.h"
#include "drivers/iccm_l3.h"
#include "plf.h"

/*
 * Cross-check the coreboot Kconfig memory anchors against the SCR-HAL
 * platform headers: a SC-DT platform update cannot silently drift from
 * the coreboot view of the memory map.
 */
#define SCR_PLF_EQ(a, b) \
	_Static_assert((uintptr_t)(a) == (uintptr_t)(b), \
		       "Kconfig memory anchor drifted from SCR-HAL plf.h: " #a " != " #b)

SCR_PLF_EQ(CONFIG_SCR_DDR_BASE, PLF_MEM_BASE);
SCR_PLF_EQ(CONFIG_SCR_OCRAM_BASE, (uintptr_t)PLF_OCRAM_BASE);
SCR_PLF_EQ(CONFIG_SCR_OCRAM_SIZE, PLF_OCRAM_SIZE);

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
#ifdef PLF_MTIMER_EXT
	/* Per-hart compare words (L3-family clusters). */
	return (uintptr_t)SCR_RTC_MTIMECMP_CORE(hart);
#else
	/* Single shared compare word (L2-family clusters). */
	(void)hart;
	return (uintptr_t)SCR_RTC_MTIMECMP;
#endif
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
 * The SCR5 MPU platforms have no PMP at all - OpenSBI configures the
 * MPU through the scr_mpu device tree node.
 */
void scr_hal_pmp_platform_init(void)
{
#if defined(PLF_PMP_SUPPORT) && (PLF_PMP_SUPPORT == 1)
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
#endif /* PLF_PMP_SUPPORT */
}

/* Both ICCM generations deliver a machine software interrupt, not an IMSIC MSI. */
void scr_hal_iccm_init(void)
{
#if defined(PLF_ICCM_L3_SUPPORT) && PLF_ICCM_L3_SUPPORT
	/* Same control setting as the Syntacore OpenSBI MMIO driver. */
	iccm_l3_write_b_control(SCR_ICCM_ICCM_CONTROL_EWE_BIT);
	fence();
#endif
}

int scr_hal_iccm_clear(void)
{
	/* Bound draining so faulty hardware cannot hang local initialization. */
	for (unsigned int i = 0; i < 64; i++) {
#if defined(PLF_ICCM_L3_SUPPORT) && PLF_ICCM_L3_SUPPORT
		if (!ipi_l3_msg_available())
			return 1;
		iccm_l3_read_msg();
#else
		if (iccm_get_data() < 0)
			return 1;
#endif
	}
	return 0;
}

int scr_hal_iccm_send(unsigned int slot)
{
#if defined(PLF_ICCM_L3_SUPPORT) && PLF_ICCM_L3_SUPPORT
	if (slot >= 64 || slot >= iccm_l3_get_harts_num())
		return 0;
	if (iccm_l3_receiver_busy(iccm_l3_get_own_id(), slot))
		return 0;
	iccm_l3_write_msg(slot, 1);
	return 1;
#else
	return iccm_send(slot, 1) != 0;
#endif
}
