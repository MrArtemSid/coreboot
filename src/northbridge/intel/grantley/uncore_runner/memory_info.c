/* SPDX-License-Identifier: GPL-2.0-only */

#include <cbmem.h>
#include <commonlib/helpers.h>
#include <console/console.h>
#include <memory_info.h>
#include <smbios.h>
#include <spd.h>
#include <stddef.h>
#include <string.h>
#undef NULL
#include "pei_services.h"

/* Version 1 memory-map HOB produced by the Grantley memory initializer. */
struct grantley_hob_dimm {
	uint8_t present;
	uint8_t x4_present;
	uint8_t ranks;
	uint8_t key_byte;
	uint8_t module_type;
	uint8_t organization;
	uint8_t density;
	uint8_t bus_width;
	uint32_t size_64m;
	uint16_t manufacturer;
	uint16_t banks;
	uint16_t secondary_type;
	uint8_t rank_map[4];
	uint8_t serial[4];
	uint8_t part_number[20];
	uint16_t max_speed_mts;
} __packed;

struct grantley_hob_channel {
	uint8_t enabled;
	uint8_t features;
	uint8_t max_dimm;
	uint8_t ranks;
	uint8_t interleave[229];
	struct grantley_hob_dimm dimm[3];
} __packed;

struct grantley_hob_node {
	uint8_t enabled;
	uint8_t channels_per_controller;
	uint8_t node_id;
	uint8_t dimm_type;
	uint16_t dram_type;
	uint8_t lrdimm_present;
	uint8_t voltage;
	uint16_t frequency_index;
	uint8_t configuration[19];
	struct grantley_hob_channel channel[4];
	uint8_t address_decode[564];
} __packed;

struct grantley_dimm_hob {
	uint8_t version;
	uint8_t entries;
	uint8_t elements[288];
	struct grantley_hob_node node[8];
	uint8_t configuration[2069];
	uint16_t frequency_index;
	uint16_t vmse_frequency;
	uint8_t mode;
	uint8_t sad_count;
	uint8_t ecc_enabled;
} __packed;

_Static_assert(sizeof(struct grantley_hob_dimm) == 48, "DIMM HOB layout");
_Static_assert(sizeof(struct grantley_hob_channel) == 377, "Channel HOB layout");
_Static_assert(sizeof(struct grantley_hob_node) == 2101, "Node HOB layout");
_Static_assert(offsetof(struct grantley_dimm_hob, node) == 290, "Node HOB offset");
_Static_assert(offsetof(struct grantley_dimm_hob, ecc_enabled) == 19173,
	       "ECC HOB offset");

static const struct grantley_dimm_hob *find_dimm_hob(
	const struct grantley_uncore_state *state)
{
	static const EFI_GUID guid = {
		0xf8870015, 0x6994, 0x4b98,
		{ 0x95, 0xa2, 0xbd, 0x56, 0xda, 0x91, 0xc0, 0x7f }
	};
	const uint8_t *p = state->hob_list;
	size_t remaining = state->hob_allocated;
	const struct grantley_dimm_hob *result = NULL;

	if (!p || remaining > state->hob_size)
		return NULL;
	while (remaining >= sizeof(EFI_HOB_GENERIC_HEADER)) {
		const EFI_HOB_GENERIC_HEADER *hob = (const void *)p;
		size_t length = hob->HobLength;

		if (length < sizeof(*hob) || length > remaining || (length & 7))
			return NULL;
		if (hob->HobType == EFI_HOB_TYPE_END_OF_HOB_LIST)
			return length == sizeof(*hob) ? result : NULL;
		if (hob->HobType == EFI_HOB_TYPE_GUID_EXTENSION &&
		    length >= sizeof(EFI_HOB_GUID_TYPE) &&
		    !memcmp(&((const EFI_HOB_GUID_TYPE *)hob)->Name, &guid, sizeof(guid))) {
			/* The payload is 19276 bytes, with up to four padding bytes. */
			size_t size = length - sizeof(EFI_HOB_GUID_TYPE);

			if (result || size < 19276 || size > ALIGN_UP(19276, 8))
				return NULL;
			result = (const void *)(p + sizeof(EFI_HOB_GUID_TYPE));
			if (result->version != 1)
				return NULL;
		}
		p += length;
		remaining -= length;
	}
	return remaining ? NULL : result;
}

static uint16_t configured_speed(uint16_t index)
{
	static const uint16_t speed[] = {
		800, 1000, 1066, 1200, 1333, 1400, 1600, 1800, 1866, 2000,
		2133, 2200, 2400, 2600, 2666, 2800, 2933, 3000, 3200,
	};

	return index < ARRAY_SIZE(speed) ? speed[index] : 0;
}

static uint16_t voltage_mv(uint8_t voltage)
{
	switch (voltage) {
	case 0: return 1500;
	case 2: return 1350;
	case 3: return 1200;
	default: return 0;
	}
}

void grantley_save_dimm_info(const struct grantley_uncore_state *state)
{
	const struct grantley_dimm_hob *hob = find_dimm_hob(state);
	unsigned int count = 0;

	if (!hob) {
		printk(BIOS_WARNING, "Grantley: DIMM memory-map HOB missing or invalid\n");
		return;
	}

	/* Validate before publishing any information to SMBIOS. */
	for (size_t n = 0; n < ARRAY_SIZE(hob->node); n++) {
		const struct grantley_hob_node *node = &hob->node[n];

		if (!node->enabled)
			continue;
		if ((node->channels_per_controller != 2 &&
		     node->channels_per_controller != 4) ||
		    (node->channels_per_controller == 4 && (n & 1)) ||
		    (node->dram_type != SPD_MEMORY_TYPE_SDRAM_DDR3 &&
		     node->dram_type != SPD_MEMORY_TYPE_DDR4_SDRAM))
			goto invalid;
		for (size_t ch = 0; ch < node->channels_per_controller; ch++) {
			const struct grantley_hob_channel *channel = &node->channel[ch];

			if (!channel->enabled)
				continue;
			if (channel->max_dimm > ARRAY_SIZE(channel->dimm))
				goto invalid;
			for (size_t d = 0; d < ARRAY_SIZE(channel->dimm); d++) {
				const struct grantley_hob_dimm *src = &channel->dimm[d];

				if (!src->present)
					continue;
				if (!src->size_64m || src->size_64m > (UINT32_MAX >> 6) ||
				    ++count > DIMM_INFO_TOTAL)
					goto invalid;
			}
		}
	}
	if (!count)
		return;

	struct memory_info *info = cbmem_add(CBMEM_ID_MEMINFO, sizeof(*info));

	if (!info) {
		printk(BIOS_ERR, "Grantley: cannot allocate DIMM information\n");
		return;
	}
	memset(info, 0, sizeof(*info));
	info->ecc_type = hob->ecc_enabled ? MEMORY_ARRAY_ECC_MULTI_BIT : MEMORY_ARRAY_ECC_NONE;
	for (size_t n = 0; n < ARRAY_SIZE(hob->node); n++) {
		const struct grantley_hob_node *node = &hob->node[n];

		if (!node->enabled)
			continue;
		for (size_t ch = 0; ch < node->channels_per_controller; ch++) {
			const struct grantley_hob_channel *channel = &node->channel[ch];

			if (!channel->enabled)
				continue;
			for (size_t d = 0; d < ARRAY_SIZE(channel->dimm); d++) {
				const struct grantley_hob_dimm *src = &channel->dimm[d];

				if (!src->present)
					continue;
				struct dimm_info *dst = &info->dimm[info->dimm_cnt++];

				dst->dimm_size = src->size_64m << 6;
				dst->ddr_type = node->dram_type == SPD_MEMORY_TYPE_DDR4_SDRAM ?
					MEMORY_TYPE_DDR4 : MEMORY_TYPE_DDR3;
				dst->soc_num = n / 2;
				dst->ctrlr_num = n % 2;
				dst->channel_num = ch + (n % 2) * node->channels_per_controller;
				dst->dimm_num = d;
				dst->rank_per_dimm = src->ranks;
				dst->mod_id = src->manufacturer;
				dst->mod_type = src->module_type & 0xf;
				dst->bus_width = src->bus_width;
				dst->vdd_voltage = voltage_mv(node->voltage);
				dst->configured_speed_mts = configured_speed(node->frequency_index);
				dst->max_speed_mts = src->max_speed_mts;
				memcpy(dst->serial, src->serial, sizeof(src->serial));
				for (size_t i = 0; i < sizeof(src->part_number); i++) {
					uint8_t c = src->part_number[i];

					dst->module_part_number[i] = c >= ' ' && c <= '~' ? c : ' ';
				}
			}
		}
	}
	printk(BIOS_INFO, "Grantley: saved %u DIMMs from memory-map HOB\n", info->dimm_cnt);
	return;

invalid:
	printk(BIOS_WARNING, "Grantley: invalid DIMM memory-map HOB data\n");
}
