/* SPDX-License-Identifier: GPL-2.0-only */

#include <cbmem.h>
#include <commonlib/helpers.h>

uintptr_t cbmem_top_chipset(void)
{
	return (uintptr_t)((uint64_t)CONFIG_MSM89X7_DDR_START +
			   (uint64_t)CONFIG_MSM89X7_DRAM_SIZE_MB * MiB);
}
