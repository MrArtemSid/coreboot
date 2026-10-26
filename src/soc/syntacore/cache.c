/* SPDX-License-Identifier: GPL-2.0-only */

#include <program_loading.h>
#include <soc/scr_hal.h>

/* SoC-local counterpart of the generic loader's optional cache hook. */
void platform_segment_loaded(uintptr_t start, size_t size, int flags)
{
	(void)flags;
	scr_hal_cache_sync_code(start, size);
}
