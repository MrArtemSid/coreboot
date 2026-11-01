/* SPDX-License-Identifier: GPL-2.0-only */

#include <program_loading.h>

/*
 * Code written through the data side is only guaranteed to be visible to
 * instruction fetch on this hart after a fence.i. Other harts that execute
 * the segment must issue their own fence.i (see smp_pause()).
 */
void arch_segment_loaded(uintptr_t start, size_t size, int flags)
{
	asm volatile ("fence.i" ::: "memory");
}
