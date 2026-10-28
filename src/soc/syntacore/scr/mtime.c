/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/encoding.h>
#include <mcall.h>
#include <soc/addressmap.h>

void mtime_init(void)
{
	HLS()->time = (void *)(SCR_MTIMER_BASE + 8);
	if (CONFIG(SCR_CLUSTER_L3))
		HLS()->timecmp = (void *)(SCR_MTIMER_BASE + 0x1000 + 8 * read_csr(mhartid));
	else
		HLS()->timecmp = (void *)(SCR_MTIMER_BASE + 16);
}
