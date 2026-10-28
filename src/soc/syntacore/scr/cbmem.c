/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <cbmem.h>
#include <commonlib/device_tree.h>
#include <mcall.h>

uintptr_t cbmem_top_chipset(void)
{
	uintptr_t top = fdt_get_memory_top(HLS()->fdt);

	ASSERT_MSG(top, "Failed reading memory range from FDT");
	return top;
}
