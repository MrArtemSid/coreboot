/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/encoding.h>
#include <mcall.h>
#include <soc/mmio.h>

void mtime_init(void)
{
	HLS()->time = (void *)(scr_mtimer_base() + 8);
	if (CONFIG(SCR_CLUSTER_L3))
		HLS()->timecmp = (void *)(scr_mtimer_base() + 0x1000 + 8 * read_csr(mhartid));
	else
		HLS()->timecmp = (void *)(scr_mtimer_base() + 16);
}
