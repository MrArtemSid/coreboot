/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/endian.h>
#include <console/console.h>
#include <stdbool.h>
#include "pei_image.h"

/* PE32 optional header up to NumberOfRvaAndSizes, without data directories. */
#define PE32_OPTIONAL_HEADER_MIN	96
/* EFI_IMAGE_SCN_MEM_EXECUTE. */
#define PE_SECTION_MEM_EXECUTE		0x20000000

/*
 * There is no loader: the PE32 image must sit at its ImageBase with a flat
 * layout (VirtualAddress == PointerToRawData) so that it can run in place.
 */
EFI_STATUS grantley_peim_entry(const void *image, size_t size, uintptr_t *entry)
{
	const uint8_t *b = image;
	uint32_t pe, opt, entry_rva;
	uint64_t table;
	uint16_t sections, opt_size;
	bool entry_ok = false;

	if (size < 0x40 || read_at_le16(b, 0) != 0x5a4d)
		goto bad;
	pe = read_at_le32(b, 0x3c);
	if (pe + 24 + 96ULL > size || read_at_le32(b, pe) != 0x00004550 ||
	    read_at_le16(b, pe + 4) != 0x014c)
		goto bad;
	sections = read_at_le16(b, pe + 6);
	opt_size = read_at_le16(b, pe + 20);
	opt = pe + 24;
	/* A smaller optional header would put the section table on top of the
	 * fields read above. */
	if (opt_size < PE32_OPTIONAL_HEADER_MIN)
		goto bad;
	table = (uint64_t)opt + opt_size;
	if (read_at_le16(b, opt) != 0x010b || table + sections * 40ULL > size)
		goto bad;
	if (read_at_le32(b, opt + 28) != (uintptr_t)image) {
		printk(BIOS_ERR, "PEIM: ImageBase 0x%x, mapped at %p\n",
		       read_at_le32(b, opt + 28), image);
		return EFI_UNSUPPORTED;
	}
	entry_rva = read_at_le32(b, opt + 16);

	for (uint16_t i = 0; i < sections; i++) {
		const uint8_t *s = b + table + i * 40;
		uint32_t vsize = read_at_le32(s, 8), va = read_at_le32(s, 12);
		uint32_t raw_size = read_at_le32(s, 16), raw_ptr = read_at_le32(s, 20);

		/* No loader creates the zero-filled tail of a virtual extent. */
		if (!vsize && !raw_size)
			continue;
		if (!raw_size || vsize > raw_size || va != raw_ptr ||
		    raw_ptr + (uint64_t)raw_size > size)
			goto bad;
		if (entry_rva >= va && entry_rva - va < raw_size &&
		    (read_at_le32(s, 36) & PE_SECTION_MEM_EXECUTE))
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
