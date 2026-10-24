/* SPDX-License-Identifier: GPL-2.0-only */

/* Machine timer services. Machine software interrupts use ICCM in iccm.c. */

#include <mcall.h>
#include <timer.h>
#include <soc/scr_hal.h>

void mtime_init(void)
{
	long hart_id = read_csr(mhartid);

	scr_iccm_init();
	if (hart_id == CONFIG_RISCV_WORKING_HARTID)
		scr_hal_mtimer_enable();

	HLS()->time = (uint64_t *)scr_hal_mtimer_time_addr();
	HLS()->timecmp = (uint64_t *)scr_hal_mtimer_cmp_addr(hart_id);
}

void timer_monotonic_get(struct mono_time *mt)
{
	uint64_t ticks = scr_hal_mtimer_now();
	unsigned long tb = scr_hal_timebase_hz();

	mt->microseconds = (uint32_t)(ticks / (tb / 1000000));
}
