/* SPDX-License-Identifier: GPL-2.0-only */

#include <cbmem.h>
#include <soc/addressmap.h>
#include <soc/plf_check.h>

uintptr_t cbmem_top_chipset(void)
{
	return SCR_CBMEM_TOP;
}
