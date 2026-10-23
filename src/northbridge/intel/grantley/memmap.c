/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/romstage.h>
#include <cbmem.h>
#include <commonlib/helpers.h>
#include <console/console.h>
#include <cpu/x86/mtrr.h>
#include <cpu/x86/smm.h>
#include <rules.h>
#include "memmap.h"

/* Private, versioned cross-stage handoff; never retain CAR/HOB pointers. */
#define GRANTLEY_CBMEM_MEMMAP 0x474d4150
#define GRANTLEY_MEMMAP_VERSION 1

struct grantley_memmap_handoff {
	uint64_t version;
	struct grantley_memory_map map;
};

#if ENV_SEPARATE_ROMSTAGE
static struct grantley_memory_map early_map;
static bool early_map_valid;
#endif

static uintptr_t memory_map_top(const struct grantley_memory_map *map)
{
	uint64_t top = 0;

	for (unsigned int i = 0; i < map->count; i++) {
		const struct grantley_memory_range *range = &map->ranges[i];
		uint64_t end = range->base + range->size;

		uint64_t aligned_end = ALIGN_DOWN(end, 8 * MiB);

		if (range->type == GRANTLEY_MEMORY_RAM && end <= map->smram_base &&
		    aligned_end >= 16 * MiB && aligned_end >= range->base + 8 * MiB &&
		    aligned_end > top)
			top = aligned_end;
	}

	return ALIGN_DOWN(top, 8 * MiB);
}

static bool memory_map_valid(const struct grantley_memory_map *map)
{
	uint64_t previous_end = 0;
	uint64_t reserved_end;

	if (!map || !map->count || map->count > GRANTLEY_MAX_MEMORY_RANGES ||
	    map->smram_base < MiB || map->smram_base >= 0x100000000ULL ||
	    map->smram_size < CONFIG_SMM_TSEG_SIZE || !map->smram_size ||
	    map->smram_size > 0x100000000ULL - map->smram_base ||
	    ((map->smram_base | map->smram_size) & 0xfff))
		return false;
	reserved_end = map->smram_base;
	for (unsigned int i = 0; i < map->count; i++) {
		const struct grantley_memory_range *range = &map->ranges[i];

		if (!range->size || range->size > UINT64_MAX - range->base ||
		    range->base < previous_end || ((range->base | range->size) & 0xfff) ||
		    (range->type != GRANTLEY_MEMORY_RAM &&
		     range->type != GRANTLEY_MEMORY_RESERVED_RAM))
			return false;
		previous_end = range->base + range->size;
		if (range->type == GRANTLEY_MEMORY_RESERVED_RAM &&
		    range->base <= reserved_end && previous_end > reserved_end)
			reserved_end = previous_end;
	}
	return reserved_end >= map->smram_base + map->smram_size && memory_map_top(map);
}

#if ENV_SEPARATE_ROMSTAGE
bool grantley_memmap_install(const struct grantley_memory_map *map)
{
	early_map_valid = false;
	if (!memory_map_valid(map))
		return false;
	early_map = *map;
	early_map_valid = true;
	return true;
}
#endif

const struct grantley_memory_map *grantley_get_memory_map(void)
{
#if ENV_SEPARATE_ROMSTAGE
	if (!early_map_valid)
		die("Grantley: memory map not installed\n");
	return &early_map;
#else
	const struct grantley_memmap_handoff *handoff = cbmem_find(GRANTLEY_CBMEM_MEMMAP);

	if (!handoff || handoff->version != GRANTLEY_MEMMAP_VERSION ||
	    !memory_map_valid(&handoff->map))
		die("Grantley: missing or invalid memory map handoff\n");
	return &handoff->map;
#endif
}

#if ENV_SEPARATE_ROMSTAGE
void grantley_memmap_save(void)
{
	const struct grantley_memory_map *map = grantley_get_memory_map();
	struct grantley_memmap_handoff *handoff = cbmem_add(GRANTLEY_CBMEM_MEMMAP,
							 sizeof(*handoff));

	if (!handoff)
		die("Grantley: cannot preserve memory map\n");
	handoff->version = GRANTLEY_MEMMAP_VERSION;
	handoff->map = *map;
}
#endif

uintptr_t cbmem_top_chipset(void)
{
	return memory_map_top(grantley_get_memory_map());
}

void smm_region(uintptr_t *start, size_t *size)
{
	const struct grantley_memory_map *map = grantley_get_memory_map();

	*start = map->smram_base;
	*size = map->smram_size;
}

void fill_postcar_frame(struct postcar_frame *pcf)
{
	const uintptr_t top = cbmem_top_chipset();

	postcar_frame_add_mtrr(pcf, top - 8 * MiB, 8 * MiB, MTRR_TYPE_WRBACK);
}
