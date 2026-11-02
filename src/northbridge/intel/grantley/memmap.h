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

/*
 * The stock SMRAM HOB reports the cacheable TSEG minus the vendor IED, while
 * the SMM region coreboot needs is the whole TSEG the IIO decodes: the generic
 * TSEG layout carves CONFIG_IED_REGION_SIZE off the top of smm_region()
 * (src/cpu/x86/smm/tseg_region.c). Bind the map's SMRAM to the IIO TSEG
 * registers (TSEG_N0 base / TSEG_N1 limit, both MiB aligned) so the map carries
 * the hardware TSEG instead of a guess. Fails closed when the HOB and the
 * registers disagree by more than ied_size, when the resulting region cannot
 * hold coreboot's SMM layout (base aligned to a power-of-two size above
 * smm_reserved_size + ied_size, below 4 GiB), and leaves the map untouched.
 *
 * ied_size is CONFIG_IED_REGION_SIZE; smm_reserved_size is
 * CONFIG_SMM_RESERVED_SIZE + CONFIG_SMM_OPAL_S3_STATE_SMRAM_SIZE.
 */
bool grantley_memmap_bind_tseg(struct grantley_memory_map *map,
			       uint32_t tseg_base_mib, uint32_t tseg_limit_mib,
			       uint64_t ied_size, uint64_t smm_reserved_size);

/* Install before cbmem_initialize_empty(), save immediately afterward. */
bool grantley_memmap_install(const struct grantley_memory_map *map);
void grantley_memmap_save(void);
const struct grantley_memory_map *grantley_get_memory_map(void);

#endif
