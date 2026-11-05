/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/helpers.h>
#include <cpu/intel/grantley/grantley_routing.h>
#include <stdbool.h>
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

enum grantley_range_position {
	RANGE_INSIDE_DRAM,
	RANGE_OUTSIDE_DRAM,
	RANGE_STRADDLES_DRAM,
};

static enum grantley_range_position range_position(uint64_t base, uint64_t size,
						   uint64_t tolm, uint64_t tohm)
{
	const uint64_t end = base + size;

	if ((base < tolm && size <= tolm - base) ||
	    (base >= 0x100000000ULL && base < tohm && size <= tohm - base))
		return RANGE_INSIDE_DRAM;
	/* Overlaps a DRAM window without being contained in it. */
	if (base < tolm || (base < tohm && end > 0x100000000ULL))
		return RANGE_STRADDLES_DRAM;
	return RANGE_OUTSIDE_DRAM;
}

static bool grantley_decode_tolm(uint32_t raw, uint64_t *end)
{
	uint64_t limit = raw & 0xfc000000ULL;

	/* Reserved low bits set means a dead config cycle. */
	if ((raw & 0x03ffffffULL) || !limit)
		return false;
	limit += GRANTLEY_MEMORY_BLOCK;
	if (limit < 64ULL * MiB || limit > 0x100000000ULL)
		return false;
	*end = limit;
	return true;
}

static bool grantley_decode_tohm(uint32_t low, uint32_t high, uint64_t *end)
{
	uint64_t limit;

	if (low & 0x03ffffffUL)
		return false;
	limit = ((uint64_t)high << 32) | (low & 0xfc000000ULL);
	limit += GRANTLEY_MEMORY_BLOCK;
	/* A limit below 4 GiB describes no high DRAM at all. */
	if (limit < 0x100000000ULL || limit > GRANTLEY_PHYS_ADDRESS_MAX)
		return false;
	*end = limit;
	return true;
}

/* Merge touching descriptors: high DRAM is reported in 4 GiB chunks. */
static bool add_range(struct grantley_memory_range *ranges, size_t *count,
		      uint64_t base, uint64_t size, uint64_t type)
{
	uint64_t end, range_end;

	for (size_t i = 0; i < *count; i++) {
		struct grantley_memory_range *range = &ranges[i];

		if (range->type != type)
			continue;
		if (base > range->base + range->size || range->base > base + size)
			continue;
		end = base + size;
		range_end = range->base + range->size;
		if (range_end > end)
			end = range_end;
		if (base < range->base)
			range->base = base;
		range->size = end - range->base;
		return true;
	}
	if (*count == GRANTLEY_MAX_MEMORY_RANGES)
		return false;
	ranges[*count] = (struct grantley_memory_range) { base, size, type };
	(*count)++;
	return true;
}

EFI_STATUS grantley_pei_memory_map(const struct grantley_uncore_state *state,
	uint32_t tolm_raw, uint32_t tohm_lo, uint32_t tohm_hi,
	struct grantley_memory_map *map)
{
	if (!state || !map || !state->hob_list ||
	    state->hob_allocated > state->hob_size ||
	    state->hob_size - state->hob_allocated < sizeof(EFI_HOB_GENERIC_HEADER))
		return EFI_INVALID_PARAMETER;
	if (!state->dram_initialized || state->memory_test_failed)
		return EFI_DEVICE_ERROR;

	uint64_t tolm, tohm;
	if (!grantley_decode_tolm(tolm_raw, &tolm) ||
	    !grantley_decode_tohm(tohm_lo, tohm_hi, &tohm))
		return EFI_DEVICE_ERROR;
	/* Low DRAM must end below the fixed ECAM window. */
	if (tolm > GRANTLEY_ECAM_BASE)
		return EFI_DEVICE_ERROR;

	struct grantley_memory_range input[GRANTLEY_MAX_MEMORY_RANGES];
	uint64_t bounds[2 * GRANTLEY_MAX_MEMORY_RANGES];
	size_t count = 0;
	struct grantley_memory_map result = { 0 };
	static const EFI_GUID smram_guid = {
		0x6dadf1d1, 0xd4cc, 0x4910,
		{ 0xbb, 0x6e, 0x82, 0xb1, 0xfd, 0x80, 0xff, 0x3d }
	};
	const uint8_t *list = state->hob_list;
	const EFI_HOB_HANDOFF_INFO_TABLE *hit = state->hob_list;
	if (state->hob_allocated < sizeof(*hit) ||
	    hit->Header.HobType != EFI_HOB_TYPE_HANDOFF ||
	    hit->Header.HobLength != sizeof(*hit) ||
	    hit->EfiEndOfHobList != (uintptr_t)(list + state->hob_allocated))
		return EFI_VOLUME_CORRUPTED;

	for (size_t off = 0; off <= state->hob_allocated;) {
		const EFI_HOB_GENERIC_HEADER *hob = (const void *)(list + off);
		if (hob->HobLength < sizeof(*hob) || (hob->HobLength & 7) || hob->Reserved)
			return EFI_VOLUME_CORRUPTED;
		if (off == state->hob_allocated) {
			if (hob->HobType != EFI_HOB_TYPE_END_OF_HOB_LIST ||
			    hob->HobLength != sizeof(*hob))
				return EFI_VOLUME_CORRUPTED;
			break;
		}
		if (hob->HobType == EFI_HOB_TYPE_END_OF_HOB_LIST ||
		    hob->HobLength > state->hob_allocated - off)
			return EFI_VOLUME_CORRUPTED;

		if (hob->HobType == EFI_HOB_TYPE_RESOURCE_DESCRIPTOR) {
			if (hob->HobLength < sizeof(EFI_HOB_RESOURCE_DESCRIPTOR))
				return EFI_VOLUME_CORRUPTED;
			const EFI_HOB_RESOURCE_DESCRIPTOR *r = (const void *)hob;
			if (r->ResourceType == EFI_RESOURCE_SYSTEM_MEMORY ||
			    r->ResourceType == EFI_RESOURCE_MEMORY_RESERVED) {
				const uint64_t base = r->PhysicalStart;
				const uint64_t size = r->ResourceLength;
				const enum grantley_range_position position =
					range_position(base, size, tolm, tohm);
				if (!size || base > UINT64_MAX - size)
					return EFI_DEVICE_ERROR;
				if (position == RANGE_INSIDE_DRAM) {
					if (!add_range(input, &count, base, size,
						       r->ResourceType == EFI_RESOURCE_SYSTEM_MEMORY ?
						       GRANTLEY_MEMORY_RAM :
						       GRANTLEY_MEMORY_RESERVED_RAM))
						return EFI_OUT_OF_RESOURCES;
				} else if (r->ResourceType == EFI_RESOURCE_SYSTEM_MEMORY ||
					   position == RANGE_STRADDLES_DRAM) {
					return EFI_DEVICE_ERROR;
				}
			}
		} else if (hob->HobType == EFI_HOB_TYPE_MEMORY_ALLOCATION) {
			if (hob->HobLength < sizeof(EFI_HOB_MEMORY_ALLOCATION))
				return EFI_VOLUME_CORRUPTED;
			const EFI_HOB_MEMORY_ALLOCATION *allocation = (const void *)hob;
			const EFI_HOB_MEMORY_ALLOCATION_HEADER *a = &allocation->AllocDescriptor;
			const uint64_t base = a->MemoryBaseAddress, size = a->MemoryLength;
			if (!size || base > UINT64_MAX - size)
				return EFI_DEVICE_ERROR;
			if (a->MemoryType != EfiConventionalMemory) {
				const enum grantley_range_position position =
					range_position(base, size, tolm, tohm);
				if (position == RANGE_INSIDE_DRAM) {
					if (!add_range(input, &count, base, size,
						       GRANTLEY_MEMORY_RESERVED_RAM))
						return EFI_OUT_OF_RESOURCES;
				} else if (position == RANGE_STRADDLES_DRAM) {
					return EFI_DEVICE_ERROR;
				}
			}
		} else if (hob->HobType == EFI_HOB_TYPE_GUID_EXTENSION) {
			if (hob->HobLength < sizeof(EFI_HOB_GUID_TYPE))
				return EFI_VOLUME_CORRUPTED;
			const EFI_HOB_GUID_TYPE *guid = (const void *)hob;
			if (!memcmp(&guid->Name, &smram_guid, sizeof(smram_guid))) {
				const uint8_t *data = (const void *)(guid + 1);
				const size_t bytes = hob->HobLength - sizeof(*guid);
				if (bytes < 8)
					return EFI_VOLUME_CORRUPTED;
				uint32_t regions;
				memcpy(&regions, data, sizeof(regions));
				if (!regions ||
				    regions > (bytes - 8) / sizeof(struct grantley_smram_descriptor))
					return EFI_VOLUME_CORRUPTED;
				size_t candidates = 0, cacheable = 0;
				uint64_t tseg_base = 0, tseg_size = 0;
				uint64_t cached_base = 0, cached_size = 0;

				for (size_t i = 0; i < regions; i++) {
					struct grantley_smram_descriptor d;
					memcpy(&d, data + 8 + i * sizeof(d), sizeof(d));
					if (!d.size || d.physical > UINT64_MAX - d.size)
						return EFI_DEVICE_ERROR;
					/* Skip the ABSEG/HSEG aliases; only TSEG is used. */
					if (d.physical < 0x100000 || d.physical >= tolm ||
					    d.size > tolm - d.physical || d.physical != d.cpu)
						continue;
					candidates++;
					tseg_base = d.physical;
					tseg_size = d.size;
					if (d.flags & GRANTLEY_SMRAM_CACHEABLE) {
						cacheable++;
						cached_base = d.physical;
						cached_size = d.size;
					}
				}
				if (cacheable == 1) {
					tseg_base = cached_base;
					tseg_size = cached_size;
				} else if (cacheable) {
					/* More than one cacheable TSEG-like region. */
					return EFI_DEVICE_ERROR;
				} else if (candidates > 1) {
					/* Several uncacheable candidates, no way to pick. */
					return EFI_DEVICE_ERROR;
				}
				if (candidates) {
					if (result.smram_size)
						return EFI_VOLUME_CORRUPTED;
					result.smram_base = tseg_base;
					result.smram_size = tseg_size;
				}
			}
		}
		off += hob->HobLength;
	}
	if (!count || !result.smram_size)
		return EFI_NOT_FOUND;

	for (size_t i = 0; i < count; i++) {
		bounds[2 * i] = input[i].base;
		bounds[2 * i + 1] = input[i].base + input[i].size;
	}
	/* Sweep all resource endpoints; reserved RAM overrides broad RAM descriptors. */
	for (size_t i = 1; i < 2 * count; i++) {
		uint64_t value = bounds[i];
		size_t j = i;
		while (j && bounds[j - 1] > value) {
			bounds[j] = bounds[j - 1];
			j--;
		}
		bounds[j] = value;
	}
	for (size_t i = 0; i + 1 < 2 * count; i++) {
		uint64_t base = bounds[i], end = bounds[i + 1], type = 0;
		if (base == end)
			continue;
		for (size_t j = 0; j < count; j++)
			if (input[j].base <= base && input[j].base + input[j].size >= end &&
			    input[j].type > type)
				type = input[j].type;
		if (!type)
			continue;
		struct grantley_memory_range *last =
			result.count ? &result.ranges[result.count - 1] : (void *)0;
		if (last && last->type == type && last->base + last->size == base) {
			last->size += end - base;
			continue;
		}
		if (result.count == GRANTLEY_MAX_MEMORY_RANGES)
			return EFI_OUT_OF_RESOURCES;
		result.ranges[result.count++] = (struct grantley_memory_range) { base, end - base, type };
	}
	bool covered = false, have_ram = false, pei_covered = false;
	/* The InstallPeiMemory() region must be low system RAM. */
	const bool pei_low = state->dram_size && state->dram_base <= 0x100000000ULL &&
			     state->dram_size <= 0x100000000ULL - state->dram_base;
	for (size_t i = 0; i < result.count; i++) {
		const struct grantley_memory_range *r = &result.ranges[i];
		have_ram |= r->type == GRANTLEY_MEMORY_RAM && r->base < tolm;
		if (pei_low && r->type == GRANTLEY_MEMORY_RAM &&
		    state->dram_base >= r->base && state->dram_base < r->base + r->size &&
		    state->dram_size <= r->base + r->size - state->dram_base)
			pei_covered = true;
		if (r->type == GRANTLEY_MEMORY_RESERVED_RAM && r->base <= result.smram_base &&
		    r->base + r->size >= result.smram_base + result.smram_size)
			covered = true;
	}
	if (!covered || !have_ram || !pei_covered)
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
