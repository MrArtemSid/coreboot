/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * coreboot machine-timer and IPI hooks for SCR clusters.
 *
 * Timer/IPI model mirrors miniboot:
 *   - time and per-hart compare words come from the SCR mtimer block,
 *     driven through the SCR-HAL register layout (scr_hal_glue.c);
 *   - there is no legacy MMIO MSIP block; inter-processor interrupts
 *     are delivered as machine MSIs through the AIA/IMSIC gateway
 *     (seteipnum_le of the target hart, miniboot's AIA_GATEWAY region).
 * When a board has no IMSIC, secondary harts simply park in wfi inside
 * smp_pause() and are never resumed (single-hart port, MAX_CPUS = 1).
 */

#include <mcall.h>
#include <timer.h>
#include <console/console.h>
#include <soc/scr_hal.h>

void mtime_init(void)
{
	long hart_id = read_csr(mhartid);

	scr_hal_mtimer_enable();

	HLS()->time = (uint64_t *)scr_hal_mtimer_time_addr();
	HLS()->timecmp = (uint64_t *)scr_hal_mtimer_cmp_addr(hart_id);
}

void set_msip(int hartid, int val)
{
	if (!val)
		return; /* coreboot only ever clears pending IPIs of harts it owns */

	if (!scr_hal_imsic_send_ipi(hartid))
		printk(BIOS_ERR, "%s: no IMSIC on this board (hart %d)\n",
		       __func__, hartid);
}

void timer_monotonic_get(struct mono_time *mt)
{
	uint64_t ticks = scr_hal_mtimer_now();
	unsigned long tb = scr_hal_timebase_hz();

	mt->microseconds = (uint32_t)(ticks / (tb / 1000000));
}
