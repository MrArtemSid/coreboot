/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#undef NULL
#include "pei_services.h"
#include "../memmap.h"

/* SMRAM GUID payload has an eight-byte header followed by 32-byte descriptors. */
struct grantley_smram_descriptor {
	UINT64 physical;
	UINT64 cpu;
	UINT64 size;
	UINT64 flags;
};

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
	uint64_t tolm = tolm_raw & 0xfc000000;
	uint64_t tohm = ((uint64_t)tohm_hi << 32) | (tohm_lo & 0xfc000000);
	if (tolm_raw == UINT32_MAX || tohm_lo == UINT32_MAX || tohm_hi == UINT32_MAX ||
	    tolm < 0x4000000 || tolm > 0x80000000 ||
	    (tohm && tohm < 0x100000000ULL) || tohm > (1ULL << 46))
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
				uint64_t base = r->PhysicalStart, size = r->ResourceLength;
				if (!size || base > UINT64_MAX - size ||
				    !((base < tolm && size <= tolm - base) ||
				      (base >= 0x100000000ULL && base < tohm && size <= tohm - base)))
					return EFI_DEVICE_ERROR;
				if (count == GRANTLEY_MAX_MEMORY_RANGES)
					return EFI_OUT_OF_RESOURCES;
				input[count] = (struct grantley_memory_range) {
					base, size, r->ResourceType == EFI_RESOURCE_SYSTEM_MEMORY ?
					GRANTLEY_MEMORY_RAM : GRANTLEY_MEMORY_RESERVED_RAM
				};
				bounds[2 * count] = base;
				bounds[2 * count + 1] = base + size;
				count++;
			}
		} else if (hob->HobType == EFI_HOB_TYPE_MEMORY_ALLOCATION) {
			if (hob->HobLength < sizeof(EFI_HOB_MEMORY_ALLOCATION))
				return EFI_VOLUME_CORRUPTED;
			const EFI_HOB_MEMORY_ALLOCATION *allocation = (const void *)hob;
			const EFI_HOB_MEMORY_ALLOCATION_HEADER *a = &allocation->AllocDescriptor;
			uint64_t base = a->MemoryBaseAddress, size = a->MemoryLength;
			if (!size || base > UINT64_MAX - size ||
			    !((base < tolm && size <= tolm - base) ||
			      (base >= 0x100000000ULL && base < tohm && size <= tohm - base)))
				return EFI_DEVICE_ERROR;
			if (a->MemoryType != EfiConventionalMemory) {
				if (count == GRANTLEY_MAX_MEMORY_RANGES)
					return EFI_OUT_OF_RESOURCES;
				input[count] = (struct grantley_memory_range) {
					base, size, GRANTLEY_MEMORY_RESERVED_RAM
				};
				bounds[2 * count] = base;
				bounds[2 * count + 1] = base + size;
				count++;
			}
		} else if (hob->HobType == EFI_HOB_TYPE_GUID_EXTENSION) {
			if (hob->HobLength < sizeof(EFI_HOB_GUID_TYPE))
				return EFI_VOLUME_CORRUPTED;
			const EFI_HOB_GUID_TYPE *guid = (const void *)hob;
			if (!memcmp(&guid->Name, &smram_guid, sizeof(smram_guid))) {
				const uint8_t *data = (const void *)(guid + 1);
				size_t bytes = hob->HobLength - sizeof(*guid);
				if (bytes < 8 || result.smram_size)
					return EFI_VOLUME_CORRUPTED;
				uint32_t regions;
				memcpy(&regions, data, sizeof(regions));
				if (!regions || regions > (bytes - 8) / sizeof(struct grantley_smram_descriptor))
					return EFI_VOLUME_CORRUPTED;
				for (size_t i = 0; i < regions; i++) {
					struct grantley_smram_descriptor d;
					memcpy(&d, data + 8 + i * sizeof(d), sizeof(d));
					if (!d.size || d.physical != d.cpu || d.physical > UINT64_MAX - d.size)
						return EFI_DEVICE_ERROR;
					/* HSEG is not the relocatable TSEG used by coreboot. */
					if (d.physical < 0x100000)
						continue;
					if (result.smram_size || d.physical >= tolm || d.size > tolm - d.physical)
						return EFI_DEVICE_ERROR;
					result.smram_base = d.physical;
					result.smram_size = d.size;
				}
			}
		}
		off += hob->HobLength;
	}
	if (!count || !result.smram_size)
		return EFI_NOT_FOUND;

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
	for (size_t i = 0; i < result.count; i++) {
		const struct grantley_memory_range *r = &result.ranges[i];
		have_ram |= r->type == GRANTLEY_MEMORY_RAM && r->base < tolm;
		if (r->type == GRANTLEY_MEMORY_RAM && state->dram_size &&
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
