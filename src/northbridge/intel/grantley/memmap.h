/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NORTHBRIDGE_INTEL_GRANTLEY_MEMMAP_H
#define NORTHBRIDGE_INTEL_GRANTLEY_MEMMAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GRANTLEY_MAX_MEMORY_RANGES 64

enum grantley_memory_type {
	GRANTLEY_MEMORY_RAM = 1,
	GRANTLEY_MEMORY_RESERVED_RAM = 2,
};

struct grantley_memory_range {
	uint64_t base;
	uint64_t size;
	uint64_t type;
};

/* Fixed-width fields keep the handoff layout identical in 32- and 64-bit stages. */
/* Sorted, disjoint HOB resource ranges. SMRAM must be covered by reserved RAM. */
struct grantley_memory_map {
	uint64_t count;
	struct grantley_memory_range ranges[GRANTLEY_MAX_MEMORY_RANGES];
	uint64_t smram_base;
	uint64_t smram_size;
};

/* Install before cbmem_initialize_empty(), save immediately afterward. */
bool grantley_memmap_install(const struct grantley_memory_map *map);
void grantley_memmap_save(void);
const struct grantley_memory_map *grantley_get_memory_map(void);

#endif
