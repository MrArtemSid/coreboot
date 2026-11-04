/* SPDX-License-Identifier: GPL-2.0-only */

#include <kconfig.h>
#include "native_contract.h"

#if CONFIG(GRANTLEY_INTELSETUP_STOCK_QD4)
#include "pei_intelsetup_stock.h"
#endif
#if CONFIG(GRANTLEY_INTELSETUP_STOCK_D4M4)
#include "pei_intelsetup_d4m4.h"
#endif

#if CONFIG(GRANTLEY_NATIVE_CONTRACT_QD4)

/*
 * X99-QD4 (huananzhi/x99-series variant qd4).
 *
 * Get64(1) is called at 0xFFDE8F91.  The stock PcdPeim database answers
 * 0x80000000 at slot db+0x40, which is also GRANTLEY_ECAM_BASE; a merely
 * aligned value would silently redirect the module's config write into
 * unrelated MMIO, so equality is required on both sides.
 *
 * Blob: 605408 bytes, sha256 f07c6904...4693, PE ImageBase 0xFFDCF9B8.
 */
static const struct grantley_native_contract contract_qd4 = {
	.board = "huananzhi/x99-series (X99-QD4)",
	.blob_size = 0x93ce0,
	.blob_sha256 = {
		0xf0, 0x7c, 0x69, 0x04, 0xe6, 0x00, 0x4b, 0xc7,
		0x17, 0xf3, 0xbe, 0x8f, 0x8d, 0x11, 0x81, 0xe0,
		0x1a, 0x8b, 0x50, 0x67, 0xb5, 0xfc, 0x46, 0xf3,
		0xec, 0xee, 0xbf, 0x16, 0x09, 0x50, 0x46, 0x93,
	},
	.image_base = 0xffdcf9b8,
	.pcd_mode = GRANTLEY_NATIVE_PCD_RUNTIME_TOKEN1,
	.pcd_token1 = 0x80000000ULL,
	.depex_size = 0x82,
	.depex_ppi_count = 7,
	.intelsetup_size = 0x17cb,
	.intelsetup_offsets_proven = true,
#if CONFIG(GRANTLEY_INTELSETUP_STOCK_QD4)
	.intelsetup_payload = grantley_intelsetup_stock_defaults,
#endif
};

#endif /* CONFIG(GRANTLEY_NATIVE_CONTRACT_QD4) */

#if CONFIG(GRANTLEY_NATIVE_CONTRACT_D4M4)

/*
 * X99D4M4 (envinda/x99d4m4).
 *
 * This build does not reference PCD_PPI at all: neither PPI GUID appears in
 * the PE, and its only indirect calls at interface+0x10 are boolean getters
 * on other PPIs.  The MMCFG base is the qword at PE+0x106B8 (VA
 * 0xFFDDEF98 = ImageBase+0x106B8), value 0x80000000; the PMBASE/ACPI_CNTL
 * writer at RVA 0x2B4A0 reads it through a cached copy and would write at
 * exactly the same ECAM offsets as the QD4 build.  The contract therefore
 * checks the constant in place of a runtime Get64 gate.
 *
 * Blob: 766368 bytes, sha256 35b74537...382d4, PE ImageBase 0xFFDCE8E0.
 * The blob is at stock image offset 0xDCE8E0 and its FFS file is
 * D71C8BA4-4AF2-4D0D-B1BA-F2409F0C20D3 with a 0x82-byte PEI_DEPEX naming the
 * same seven PPIs as QD4.  Its own stock IntelSetup default is 6090 bytes
 * (NVAR at 0x800202).
 */
static const struct grantley_native_contract contract_d4m4 = {
	.board = "envinda/x99d4m4 (X99D4M4)",
	.blob_size = 0xbb1a0,
	.blob_sha256 = {
		0x35, 0xb7, 0x45, 0x37, 0x35, 0x6d, 0x86, 0xeb,
		0xcc, 0x6c, 0x77, 0x8d, 0x64, 0x6b, 0xe1, 0x2f,
		0xcf, 0x86, 0xf8, 0xf5, 0xdc, 0xce, 0x5d, 0xae,
		0x11, 0xa4, 0x8f, 0x78, 0xbb, 0xe3, 0x82, 0xd4,
	},
	.image_base = 0xffdce8e0,
	.pcd_mode = GRANTLEY_NATIVE_PCD_BLOB_ECAM,
	.pcd_ecam_offset = 0x106b8,
	.pcd_ecam_value = 0x80000000,
	.pcd_ecam_store_offset = 0x2b484,
	.depex_size = 0x82,
	.depex_ppi_count = 7,
	.intelsetup_size = 0x17ca,
	/*
	 * Payload identity is proven (this board's NVAR bytes), field semantics
	 * are not: 0x1248/0x11FF only decode to plausible enums when the QD4
	 * offsets are applied, and this board's machinecontract lists consumed
	 * offsets 0x74/0x2D6/0x2D9/0x2DB and no 0x11FF consumer.  So the
	 * payload is served verbatim and no byte is overridden.
	 */
	.intelsetup_offsets_proven = false,
#if CONFIG(GRANTLEY_INTELSETUP_STOCK_D4M4)
	.intelsetup_payload = grantley_intelsetup_d4m4_defaults,
#endif
};

#endif /* CONFIG(GRANTLEY_NATIVE_CONTRACT_D4M4) */

const struct grantley_native_contract *grantley_native_contract(void)
{
#if CONFIG(GRANTLEY_NATIVE_CONTRACT_QD4)
	return &contract_qd4;
#elif CONFIG(GRANTLEY_NATIVE_CONTRACT_D4M4)
	return &contract_d4m4;
#else
	return NULL;
#endif
}

static uint32_t read_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

/*
 * BLOB_ECAM gate.  The pinned X99D4M4 module carries its ECAM base in its own
 * .text and copies it to a module global at entry:
 *
 *   0x2B484: A1 98 EF DD FF   mov eax, [0xFFDDEF98]   ; Datum = ImageBase+0x106B8
 *   0x2B489: A3 84 69 E8 FF   mov [0xFFE86984], eax   ; module global
 *
 * The check fails closed if either the datum value or the copy instruction is
 * not the one the contract describes, so a different blob build cannot enter
 * with an MMCFG base nobody verified.
 */
const char *grantley_native_check_blob_ecam(const void *image, size_t size)
{
	const struct grantley_native_contract *contract = grantley_native_contract();

	if (!contract)
		return "no native contract is configured for this board";
	if (contract->pcd_mode != GRANTLEY_NATIVE_PCD_BLOB_ECAM)
		return (const char *)0;
	if (!image)
		return "no image to check";
	if (size < (size_t)contract->pcd_ecam_offset + sizeof(uint32_t))
		return "image is too small to hold the contract's ECAM datum";

	const uint8_t *bytes = image;
	const uint32_t value = read_le32(bytes + contract->pcd_ecam_offset);
	if (value != contract->pcd_ecam_value)
		return "image ECAM datum does not equal the contract value";

	if (size < (size_t)contract->pcd_ecam_store_offset + 10)
		return "image is too small to hold the ECAM copy at entry";
	const uint8_t *store = bytes + contract->pcd_ecam_store_offset;
	if (store[0] != 0xa1 || read_le32(store + 1) !=
	    (uint32_t)(contract->image_base + contract->pcd_ecam_offset))
		return "image does not copy its ECAM datum at entry";

	return (const char *)0;
}
