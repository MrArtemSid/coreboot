/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/helpers.h>
#include <cpu/intel/grantley/grantley_routing.h>
#include <stdint.h>
#include <string.h>
#undef NULL
#include "pei_services.h"
#include "../memmap.h"

/* SMRAM GUID payload: four-byte region count, four bytes padding, then
 * 32-byte descriptors (MdePkg/Guid/SmramMemoryReserve.h). */
struct grantley_smram_descriptor {
	UINT64 physical;
	UINT64 cpu;
	UINT64 size;
	UINT64 flags;
};

/* RegionState bit from PiMultiPhase.h. The stock RC marks the TSEG cacheable
 * and reports the ABSEG/HSEG aliases without it. */
#define GRANTLEY_SMRAM_CACHEABLE 0x8ULL

/* TOLM/TOHM hold the last decoded 64 MiB block, not the end address. */
#define GRANTLEY_MEMORY_BLOCK (64ULL * MiB)
#define GRANTLEY_PHYS_ADDRESS_MAX (1ULL << 46)

static bool grantley_decode_tolm(uint32_t raw, uint64_t *end)
{
	/* Reserved low bits set means a dead config cycle. */
	if ((raw & 0x03ffffff) || !raw)
		return false;
	*end = (raw & 0xfc000000ULL) + GRANTLEY_MEMORY_BLOCK;
	return *end <= 0x100000000ULL;
}

static bool grantley_decode_tohm(uint32_t low, uint32_t high, uint64_t *end)
{
	if (low & 0x03ffffff)
		return false;
	*end = ((uint64_t)high << 32 | (low & 0xfc000000)) + GRANTLEY_MEMORY_BLOCK;
	return *end >= 0x100000000ULL && *end <= GRANTLEY_PHYS_ADDRESS_MAX;
}

static bool inside_dram(uint64_t base, uint64_t size, uint64_t tolm, uint64_t tohm)
{
	return (base < tolm && size <= tolm - base) ||
	       (base >= 0x100000000ULL && base < tohm && size <= tohm - base);
}

static bool outside_dram(uint64_t base, uint64_t size, uint64_t tolm, uint64_t tohm)
{
	return base >= tolm && (base + size <= 0x100000000ULL || base >= tohm);
}

/* Insert sorted; merge with a touching range of the same type. */
static bool add_range(struct grantley_memory_map *map, uint64_t base,
		      uint64_t size, uint64_t type)
{
	size_t i = 0;

	while (i < map->count && map->ranges[i].base < base)
		i++;
	if ((i && map->ranges[i - 1].base + map->ranges[i - 1].size > base) ||
	    (i < map->count && base + size > map->ranges[i].base))
		return false;
	if (i && map->ranges[i - 1].type == type &&
	    map->ranges[i - 1].base + map->ranges[i - 1].size == base) {
		map->ranges[i - 1].size += size;
		return true;
	}
	if (map->count == GRANTLEY_MAX_MEMORY_RANGES)
		return false;
	memmove(&map->ranges[i + 1], &map->ranges[i],
		(map->count - i) * sizeof(map->ranges[0]));
	map->ranges[i] = (struct grantley_memory_range) { base, size, type };
	map->count++;
	return true;
}

/*
 * The producer writes the END_OF_HOB_LIST marker at hob_allocated and does not
 * count it, so the verified region ends after that marker. Whatever follows it
 * is unused arena and must not be read as a HOB.
 */
static bool grantley_hob_list_begin(const struct grantley_uncore_state *state,
				    const EFI_HOB_GENERIC_HEADER **hob,
				    size_t *remaining)
{
	if (!state->hob_list || state->hob_allocated > state->hob_size)
		return false;
	if (state->hob_size - state->hob_allocated < sizeof(EFI_HOB_GENERIC_HEADER))
		return false;
	*hob = state->hob_list;
	*remaining = state->hob_allocated + sizeof(EFI_HOB_GENERIC_HEADER);
	return true;
}

/* HOBs are 8-byte aligned and each one must fit in the verified region. */
static bool grantley_hob_next(const EFI_HOB_GENERIC_HEADER *hob, size_t remaining,
			      const EFI_HOB_GENERIC_HEADER **next,
			      size_t *next_remaining)
{
	size_t length;

	if (remaining < sizeof(*hob))
		return false;
	length = hob->HobLength;
	if (length < sizeof(*hob) || (length & 7) || length > remaining)
		return false;
	*next = (const EFI_HOB_GENERIC_HEADER *)((const uint8_t *)hob + length);
	*next_remaining = remaining - length;
	return true;
}

/* The module reports system/reserved memory resource HOBs and one SMRAM GUID HOB. */
EFI_STATUS grantley_pei_memory_map(const struct grantley_uncore_state *state,
	uint32_t tolm_raw, uint32_t tohm_lo, uint32_t tohm_hi,
	struct grantley_memory_map *map)
{
	static const EFI_GUID smram_guid = {
		0x6dadf1d1, 0xd4cc, 0x4910,
		{ 0xbb, 0x6e, 0x82, 0xb1, 0xfd, 0x80, 0xff, 0x3d }
	};
	struct grantley_memory_map result = { 0 };
	const EFI_HOB_GENERIC_HEADER *hob;
	size_t remaining;
	uint64_t tolm, tohm;
	bool pei_covered = false, smram_covered = false;

	if (!state->dram_initialized)
		return EFI_DEVICE_ERROR;
	/* Low DRAM must end below the fixed ECAM window. */
	if (!grantley_decode_tolm(tolm_raw, &tolm) || tolm > GRANTLEY_ECAM_BASE ||
	    !grantley_decode_tohm(tohm_lo, tohm_hi, &tohm))
		return EFI_DEVICE_ERROR;

	if (!grantley_hob_list_begin(state, &hob, &remaining))
		return EFI_VOLUME_CORRUPTED;

	for (;;) {
		const EFI_HOB_GENERIC_HEADER *next;
		size_t next_remaining;

		if (!grantley_hob_next(hob, remaining, &next, &next_remaining))
			return EFI_VOLUME_CORRUPTED;

		if (hob->HobType == EFI_HOB_TYPE_END_OF_HOB_LIST) {
			/* The terminator carries no payload. */
			if (hob->HobLength != sizeof(*hob))
				return EFI_VOLUME_CORRUPTED;
			break;
		}

		if (hob->HobType == EFI_HOB_TYPE_RESOURCE_DESCRIPTOR) {
			const EFI_HOB_RESOURCE_DESCRIPTOR *r;
			bool ram;

			if (hob->HobLength < sizeof(EFI_HOB_RESOURCE_DESCRIPTOR))
				return EFI_VOLUME_CORRUPTED;
			r = (const void *)hob;
			ram = r->ResourceType == EFI_RESOURCE_SYSTEM_MEMORY;
			if (!ram && r->ResourceType != EFI_RESOURCE_MEMORY_RESERVED)
				goto next_hob;
			/* Reserved MMIO outside DRAM does not carve RAM. */
			if (!ram && outside_dram(r->PhysicalStart, r->ResourceLength,
						 tolm, tohm))
				goto next_hob;
			if (!r->ResourceLength ||
			    !inside_dram(r->PhysicalStart, r->ResourceLength, tolm, tohm) ||
			    !add_range(&result, r->PhysicalStart, r->ResourceLength,
				       ram ? GRANTLEY_MEMORY_RAM :
					     GRANTLEY_MEMORY_RESERVED_RAM))
				return EFI_DEVICE_ERROR;
		} else if (hob->HobType == EFI_HOB_TYPE_GUID_EXTENSION) {
			const UINT8 *data;
			size_t payload;
			uint32_t regions;

			if (hob->HobLength < sizeof(EFI_HOB_GUID_TYPE))
				return EFI_VOLUME_CORRUPTED;
			if (memcmp(&((const EFI_HOB_GUID_TYPE *)hob)->Name, &smram_guid,
				   sizeof(smram_guid)))
				goto next_hob;

			data = (const UINT8 *)hob + sizeof(EFI_HOB_GUID_TYPE);
			payload = hob->HobLength - sizeof(EFI_HOB_GUID_TYPE);
			/* The payload is a u32 region count, u32 padding, then the
			 * descriptors. */
			if (payload < 8)
				return EFI_VOLUME_CORRUPTED;
			memcpy(&regions, data, sizeof(regions));
			if ((uint64_t)regions * sizeof(struct grantley_smram_descriptor) >
			    payload - 8)
				return EFI_VOLUME_CORRUPTED;
			/* Only the TSEG is cacheable; ABSEG/HSEG are aliases. */
			for (uint32_t i = 0; i < regions; i++) {
				struct grantley_smram_descriptor d;

				memcpy(&d, data + 8 + i * sizeof(d), sizeof(d));
				if (!(d.flags & GRANTLEY_SMRAM_CACHEABLE))
					continue;
				if (result.smram_size || !d.size || d.physical != d.cpu ||
				    d.physical < MiB || d.physical >= tolm ||
				    d.size > tolm - d.physical)
					return EFI_DEVICE_ERROR;
				result.smram_base = d.physical;
				result.smram_size = d.size;
			}
		}
next_hob:
		hob = next;
		remaining = next_remaining;
	}
	if (!result.smram_size)
		return EFI_NOT_FOUND;

	for (size_t i = 0; i < result.count; i++) {
		const struct grantley_memory_range *r = &result.ranges[i];
		const uint64_t end = r->base + r->size;

		if (r->type == GRANTLEY_MEMORY_RAM && state->dram_base >= r->base &&
		    state->dram_size <= end - state->dram_base && state->dram_base < end)
			pei_covered = true;
		if (r->type == GRANTLEY_MEMORY_RESERVED_RAM &&
		    r->base <= result.smram_base &&
		    end >= result.smram_base + result.smram_size)
			smram_covered = true;
	}
	if (!pei_covered || !smram_covered)
		return EFI_DEVICE_ERROR;
	*map = result;
	return EFI_SUCCESS;
}

/* TSEG must fit coreboot SMM and be describable by SMRR. */
static bool smm_layout_fits(uint64_t base, uint64_t size, uint64_t ied_size,
			    uint64_t smm_reserved_size)
{
	return size > ied_size + smm_reserved_size && !(size & (size - 1)) &&
	       !(base & (size - 1)) && base + size <= 0x100000000ULL;
}

bool grantley_memmap_bind_tseg(struct grantley_memory_map *map,
			       uint32_t tseg_base_mib, uint32_t tseg_limit_mib,
			       uint64_t ied_size, uint64_t smm_reserved_size)
{
	/* TSEG_N0/N1 hold the first and last MiB of the TSEG. */
	const uint64_t base = tseg_base_mib & 0xfff00000ULL;
	const uint64_t end = (tseg_limit_mib & 0xfff00000ULL) + MiB;

	if (!map || !map->smram_size ||
	    map->smram_base > UINT64_MAX - map->smram_size)
		return false;
	if (tseg_base_mib == UINT32_MAX || tseg_limit_mib == UINT32_MAX || base >= end)
		return false;
	if (base != map->smram_base || end < map->smram_base + map->smram_size)
		return false;
	/* The SMRAM HOB omits only the IED at the top of TSEG. */
	if (end - (map->smram_base + map->smram_size) > ied_size)
		return false;
	if (!smm_layout_fits(base, end - base, ied_size, smm_reserved_size))
		return false;
	map->smram_size = end - base;
	return true;
}
