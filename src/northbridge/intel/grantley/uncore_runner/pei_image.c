/* SPDX-License-Identifier: GPL-2.0-only */

#include <string.h>
#include <console/console.h>
#include <vb2_sha.h>
#include "native_contract.h"
#include "pei_image.h"

/* The authorized digest is the board contract's pinned blob, not a constant
 * in this file: a changed pin must be a change to the board descriptor, and a
 * board without a descriptor refuses every image.
 */
#if ENV_TEST
static bool test_authorize_any;

void grantley_peim_test_authorize_any(bool enable)
{
	test_authorize_any = enable;
}
#endif

/* PE/COFF constants (Microsoft PE32 specification). */
#define PE_DOS_SIGNATURE		0x5a4d
#define PE_SIGNATURE			0x00004550
#define PE_MACHINE_I386			0x014c
#define PE32_MAGIC			0x010b
#define PE_DOS_LFANEW_OFFSET		0x3c
#define PE_FILE_HEADER_SIZE		20
#define PE_OPTIONAL_MIN_SIZE		96
#define PE_DIR_BASE_RELOCATION		5
#define PE_SCN_CNT_CODE			0x00000020
#define PE_SCN_MEM_EXECUTE		0x20000000
/* A 32-bit image cannot be mapped above the 4 GiB linear boundary. */
#define PE_MAX_IMAGE_SIZE		0xffffffffU

struct pe_section {
	uint32_t va;
	uint32_t virtual_size;
	uint32_t raw_ptr;
	uint32_t raw_size;
	uint32_t characteristics;
};

static inline uint16_t pe_u16(const void *p, size_t off)
{
	const uint8_t *b = p;
	return (uint16_t)(b[off] | (b[off + 1] << 8));
}

static inline uint32_t pe_u32(const void *p, size_t off)
{
	const uint8_t *b = p;
	return (uint32_t)b[off] | ((uint32_t)b[off + 1] << 8) |
	       ((uint32_t)b[off + 2] << 16) | ((uint32_t)b[off + 3] << 24);
}

/* Returns the section containing rva, or NULL. */
static const uint8_t *pe_find_section(const void *image, size_t size,
				      uint32_t rva, struct pe_section *out)
{
	const uint8_t *b = image;
	const uint32_t e_lfanew = pe_u32(image, PE_DOS_LFANEW_OFFSET);
	const uint16_t sections = pe_u16(image, e_lfanew + 6);
	const uint16_t opt_size = pe_u16(image, e_lfanew + PE_FILE_HEADER_SIZE);
	const size_t table = (size_t)e_lfanew + 24 + opt_size;

	for (uint16_t i = 0; i < sections; i++) {
		const size_t off = table + (size_t)i * 40;
		if (off + 40 > size)
			return NULL;
		struct pe_section sc = {
			.virtual_size = pe_u32(b, off + 8),
			.va = pe_u32(b, off + 12),
			.raw_size = pe_u32(b, off + 16),
			.raw_ptr = pe_u32(b, off + 20),
			.characteristics = pe_u32(b, off + 36),
		};
		uint32_t span = sc.virtual_size ? sc.virtual_size : sc.raw_size;
		if (rva >= sc.va && rva < sc.va + span) {
			if (out)
				*out = sc;
			return b + off;
		}
	}

	return NULL;
}

EFI_STATUS grantley_peim_verify(const void *image, size_t size,
				struct grantley_peim_image *out)
{
	if (!image || size < PE_DOS_LFANEW_OFFSET + 4)
		return EFI_UNSUPPORTED;
	if (pe_u16(image, 0) != PE_DOS_SIGNATURE) {
		printk(BIOS_ERR, "PEIM: missing MZ signature\n");
		return EFI_UNSUPPORTED;
	}

	const uint32_t e_lfanew = pe_u32(image, PE_DOS_LFANEW_OFFSET);
	if (e_lfanew > size || size - e_lfanew < 24 + PE_OPTIONAL_MIN_SIZE) {
		printk(BIOS_ERR, "PEIM: e_lfanew 0x%x outside %zu byte image\n",
		       e_lfanew, size);
		return EFI_UNSUPPORTED;
	}
	if (pe_u32(image, e_lfanew) != PE_SIGNATURE) {
		printk(BIOS_ERR, "PEIM: missing PE signature\n");
		return EFI_UNSUPPORTED;
	}

	const uint16_t machine = pe_u16(image, e_lfanew + 4);
	const uint16_t sections = pe_u16(image, e_lfanew + 6);
	const uint16_t opt_size = pe_u16(image, e_lfanew + PE_FILE_HEADER_SIZE);
	if (machine != PE_MACHINE_I386) {
		printk(BIOS_ERR, "PEIM: machine 0x%x is not i386\n", machine);
		return EFI_UNSUPPORTED;
	}
	if (!sections) {
		printk(BIOS_ERR, "PEIM: no sections\n");
		return EFI_UNSUPPORTED;
	}

	const size_t opt = (size_t)e_lfanew + 24;
	const size_t table = opt + opt_size;
	if (opt_size < PE_OPTIONAL_MIN_SIZE || table + (size_t)sections * 40 > size)
		return EFI_UNSUPPORTED;
	if (pe_u16(image, opt) != PE32_MAGIC) {
		printk(BIOS_ERR, "PEIM: optional header magic 0x%x is not PE32\n",
		       pe_u16(image, opt));
		return EFI_UNSUPPORTED;
	}

	struct grantley_peim_image info = {
		.entry_rva = pe_u32(image, opt + 16),
		.image_base = pe_u32(image, opt + 28),
		.image_size = pe_u32(image, opt + 56),
		.headers_size = pe_u32(image, opt + 60),
		.sections = sections,
		.xip_compatible = true,
	};

	if (!info.image_size || info.image_size > PE_MAX_IMAGE_SIZE ||
	    info.headers_size > info.image_size ||
	    info.headers_size > size) {
		printk(BIOS_ERR, "PEIM: bad image geometry (size 0x%x)\n",
		       info.image_size);
		return EFI_UNSUPPORTED;
	}

	if (pe_u32(image, opt + 92) > PE_DIR_BASE_RELOCATION) {
		info.reloc_rva = pe_u32(image, opt + 96 + 8 * PE_DIR_BASE_RELOCATION);
		info.reloc_size = pe_u32(image, opt + 100 + 8 * PE_DIR_BASE_RELOCATION);
		if (!info.reloc_rva != !info.reloc_size)
			return EFI_UNSUPPORTED;
		if (info.reloc_rva) {
			if (info.reloc_rva >= info.image_size ||
			    info.reloc_size > info.image_size - info.reloc_rva ||
			    (size_t)info.reloc_rva + info.reloc_size > size) {
				printk(BIOS_ERR, "PEIM: relocation directory outside image\n");
				return EFI_UNSUPPORTED;
			}
		}
	}

	/* Every section must be inside the image; in-place execution additionally
	 * requires the file layout to be flat (VirtualAddress == PointerToRawData).
	 */
	for (uint16_t i = 0; i < sections; i++) {
		const size_t off = table + (size_t)i * 40;
		const uint32_t vsize = pe_u32(image, off + 8);
		const uint32_t va = pe_u32(image, off + 12);
		const uint32_t raw_size = pe_u32(image, off + 16);
		const uint32_t raw_ptr = pe_u32(image, off + 20);

		const uint32_t span = vsize ? vsize : raw_size;
		if (va >= info.image_size || span > info.image_size - va) {
			printk(BIOS_ERR, "PEIM: section %u outside SizeOfImage\n", i);
			return EFI_UNSUPPORTED;
		}
		if (raw_size && ((size_t)raw_ptr + raw_size > size ||
				 raw_ptr >= info.image_size)) {
			printk(BIOS_ERR, "PEIM: section %u raw data outside file\n", i);
			return EFI_UNSUPPORTED;
		}
		/* In-place execution reads file bytes at VA.  A section may have
		 * SizeOfRawData padded above VirtualSize (normal PE alignment),
		 * but it may not have a virtual hole (span > raw_size) or a
		 * different file offset.
		 */
		if (raw_size && (va != raw_ptr || span > raw_size))
			info.xip_compatible = false;
	}

	struct pe_section entry_section;
	if (!pe_find_section(image, size, info.entry_rva, &entry_section) ||
	    !(entry_section.characteristics &
	      (PE_SCN_MEM_EXECUTE | PE_SCN_CNT_CODE))) {
		printk(BIOS_ERR, "PEIM: entry 0x%x is not in an executable section\n",
		       info.entry_rva);
		return EFI_UNSUPPORTED;
	}
	/* The entry instruction itself must be backed by file bytes. */
	if (info.entry_rva - entry_section.va >= entry_section.raw_size) {
		printk(BIOS_ERR, "PEIM: entry 0x%x is outside the section's raw data\n",
		       info.entry_rva);
		return EFI_UNSUPPORTED;
	}

	if (out)
		*out = info;

	return EFI_SUCCESS;
}

bool grantley_peim_sha256(const void *image, size_t size,
			  uint8_t digest[GRANTLEY_PEIM_SHA256_SIZE])
{
	if (!image || !digest || size > UINT32_MAX)
		return false;

	struct vb2_sha256_context ctx;
	vb2_sha256_init(&ctx, VB2_HASH_SHA256);
	vb2_sha256_update(&ctx, image, (uint32_t)size);
	vb2_sha256_finalize(&ctx, digest, VB2_HASH_SHA256);
	return true;
}

const uint8_t *grantley_peim_authorized_sha256(void)
{
	const struct grantley_native_contract *contract = grantley_native_contract();

	return contract ? contract->blob_sha256 : (const uint8_t *)0;
}

bool grantley_peim_authorized(const void *image, size_t size)
{
#if ENV_TEST
	if (test_authorize_any)
		return true;
#endif
	const struct grantley_native_contract *contract = grantley_native_contract();

	/* Size and digest belong together: hashing a different-length file can
	 * never match, but naming the mismatch makes a wrong-board blob
	 * obvious in the log instead of a bare digest difference.
	 */
	if (!contract || size != contract->blob_size)
		return false;

	uint8_t digest[GRANTLEY_PEIM_SHA256_SIZE];

	if (!grantley_peim_sha256(image, size, digest))
		return false;
	return memcmp(digest, contract->blob_sha256, sizeof(digest)) == 0;
}
