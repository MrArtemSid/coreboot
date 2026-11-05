/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <string.h>
#include "pei_image.h"

static uint16_t pe_u16(const uint8_t *b, size_t off)
{
	uint16_t v;

	memcpy(&v, b + off, sizeof(v));
	return v;
}

static uint32_t pe_u32(const uint8_t *b, size_t off)
{
	uint32_t v;

	memcpy(&v, b + off, sizeof(v));
	return v;
}

/*
 * There is no loader: the PE32 image must sit at its ImageBase with a flat
 * layout (VirtualAddress == PointerToRawData) so that it can run in place.
 */
EFI_STATUS grantley_peim_entry(const void *image, size_t size, uintptr_t *entry)
{
	const uint8_t *b = image;
	uint32_t pe, opt, table, entry_rva;
	uint16_t sections;
	bool entry_ok = false;

	if (size < 0x40 || pe_u16(b, 0) != 0x5a4d)
		goto bad;
	pe = pe_u32(b, 0x3c);
	if (pe + 24 + 96ULL > size || pe_u32(b, pe) != 0x00004550 ||
	    pe_u16(b, pe + 4) != 0x014c)
		goto bad;
	sections = pe_u16(b, pe + 6);
	opt = pe + 24;
	table = opt + pe_u16(b, pe + 20);
	if (pe_u16(b, opt) != 0x010b || table + sections * 40ULL > size)
		goto bad;
	if (pe_u32(b, opt + 28) != (uintptr_t)image) {
		printk(BIOS_ERR, "PEIM: ImageBase 0x%x, mapped at %p\n",
		       pe_u32(b, opt + 28), image);
		return EFI_UNSUPPORTED;
	}
	entry_rva = pe_u32(b, opt + 16);

	for (uint16_t i = 0; i < sections; i++) {
		const uint8_t *s = b + table + i * 40;
		uint32_t vsize = pe_u32(s, 8), va = pe_u32(s, 12);
		uint32_t raw_size = pe_u32(s, 16), raw_ptr = pe_u32(s, 20);

		if (raw_size && (va != raw_ptr || vsize > raw_size ||
				 raw_ptr + (uint64_t)raw_size > size))
			goto bad;
		if (entry_rva >= va && entry_rva - va < raw_size &&
		    (pe_u32(s, 36) & 0x20000020))
			entry_ok = true;
	}
	if (!entry_ok)
		goto bad;

	*entry = (uintptr_t)image + entry_rva;
	return EFI_SUCCESS;
bad:
	printk(BIOS_ERR, "PEIM: not an in-place PE32 i386 image\n");
	return EFI_UNSUPPORTED;
}
