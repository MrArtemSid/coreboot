/* SPDX-License-Identifier: GPL-2.0-only */

/* Machine timer services. Machine software interrupts use ICCM in iccm.c. */

#include <console/console.h>
#include <mcall.h>
#include <timer.h>
#include <soc/scr_hal.h>

void mtime_init(void)
{
	long hart_id = read_csr(mhartid);

	scr_hart_init();
	if (ENV_BOOTBLOCK && hart_id == CONFIG_RISCV_WORKING_HARTID)
		scr_hal_mtimer_enable();

	HLS()->time = (uint64_t *)scr_hal_mtimer_time_addr();
	HLS()->timecmp = (uint64_t *)scr_hal_mtimer_cmp_addr(hart_id);
}

void timer_monotonic_get(struct mono_time *mt)
{
	uint64_t ticks = scr_hal_mtimer_now();
	unsigned long tb = scr_hal_timebase_hz();

	/* Split before scaling: preserve uptime and non-integral MHz timebases. */
	if (!tb || tb > UINT32_MAX)
		die("Invalid SCR timer timebase");
	mt->microseconds = (ticks / tb) * USECS_PER_SEC +
		(ticks % tb) * USECS_PER_SEC / tb;
}
