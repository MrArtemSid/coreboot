/* SPDX-License-Identifier: GPL-2.0-only */

#include <bootblock_common.h>
#include <soc/mmu.h>

void bootblock_soc_early_init(void)
{
	/* Clocks and console are already initialized by the primary
	 * bootloader. Nothing to do here. */
}

void bootblock_soc_init(void)
{
	msm89x7_mmu_init();
}
