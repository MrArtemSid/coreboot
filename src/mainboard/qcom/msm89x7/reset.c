/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/mmio.h>
#include <reset.h>

/* PS_HOLD is used on msm89x7 like on the earlier msm8916 to trigger a
 * warm reset through the PMIC. */
#define GCNT_PSHOLD	((void *)0x004AB000u)

void do_board_reset(void)
{
	write32(GCNT_PSHOLD, 0);
}
