/* SPDX-License-Identifier: GPL-2.0-only */

#include <program_loading.h>

void cache_l1_flush(void *start, long size);

void platform_segment_loaded(uintptr_t start, size_t size, int flags)
{
	cache_l1_flush((void *)start, size);
}
