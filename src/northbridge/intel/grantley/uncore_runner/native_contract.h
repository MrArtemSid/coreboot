/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NORTHBRIDGE_INTEL_GRANTLEY_NATIVE_CONTRACT_H
#define NORTHBRIDGE_INTEL_GRANTLEY_NATIVE_CONTRACT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * One board, one native-entry contract.
 *
 * Everything the runner needs in order to enter the vendor UncoreInitPeim is
 * described here and nowhere else: the authorized blob (size, SHA-256 and the
 * PE ImageBase it must be mapped at), the way the module obtains its
 * MMCFG/ECAM base, the PEI_DEPEX it was built against, and whether a stock
 * IntelSetup payload is compiled in for this board.
 *
 * The table is selected from Kconfig by GRANTLEY_NATIVE_CONTRACT_<BOARD>,
 * which each mainboard Kconfig defaults for exactly one board.  There is no
 * runtime board probing and no "nearest" fallback: a board without a contract
 * symbol has no native entry at all.
 *
 * Both supported builds are different in a way that matters:
 *
 *   x99-series (QD4)  the module calls PCD_PPI->Get64(1) at entry and uses the
 *                     returned value as the MMCFG base.  The value must be
 *                     supplied at runtime and must EQUAL the base the port
 *                     programs (reports/reverse-pcd-value.md).
 *   x99d4m4           the module does not reference PCD_PPI at all (the
 *                     16-byte PPI GUID does not occur in its 766368 bytes).
 *                     Its ECAM base is a datum inside its own .text at
 *                     PE+0x106B8, copied to a module global at entry
 *                     (0xFFDF9D64).  Supplying a runtime token would not
 *                     change what executes, and the stock PCD database's
 *                     token 1 on this image is 0x1C200, not the ECAM base, so
 *                     the contract records the datum offset/value and the
 *                     runner checks those bytes (and that the PPI is absent)
 *                     before entry.
 *
 * Both boards' PEI_DEPEX name the same seven PPIs (0x82-byte expression, six
 * ANDs), including BaseMemoryTest (B6EC423C), so the runner's existing depex
 * set is reused unchanged.
 */
#define GRANTLEY_NATIVE_SHA256_SIZE 32

enum grantley_native_pcd_mode {
	/* PCD_PPI->Get64(1) at entry; the supplied value must match exactly. */
	GRANTLEY_NATIVE_PCD_RUNTIME_TOKEN1 = 1,
	/* No PCD_PPI in this blob: the ECAM base is its own .text datum and the
	 * module copies it at entry.  Nothing is supplied at runtime. */
	GRANTLEY_NATIVE_PCD_BLOB_ECAM = 2,
};

struct grantley_native_contract {
	/* Stable name used in logs. */
	const char *board;

	/* The only blob this board may execute. */
	uint32_t blob_size;
	uint8_t blob_sha256[GRANTLEY_NATIVE_SHA256_SIZE];
	/* PE ImageBase; the image is executed in place, so this is also the
	 * address at which CBFS must map it.
	 */
	uint32_t image_base;

	/* How the MMCFG/ECAM base reaches the module. */
	enum grantley_native_pcd_mode pcd_mode;
	/* RUNTIME_TOKEN1: the exact Get64(1) value. */
	uint64_t pcd_token1;
	/* BLOB_ECAM: PE offset and value of the module's own ECAM datum, and
	 * the PE offset of the `mov eax,[datum]; mov [global],eax` copy that
	 * runs at entry. */
	uint32_t pcd_ecam_offset;
	uint32_t pcd_ecam_value;
	uint32_t pcd_ecam_store_offset;

	/* PEI_DEPEX the module was built against.  The GUID set itself lives in
	 * pei_services.c because both boards use the same one.
	 */
	uint32_t depex_size;
	uint8_t depex_ppi_count;

	/* Board's own stock IntelSetup payload, or NULL when not compiled in.
	 * Only the board-specific Kconfig option can make this non-NULL.
	 */
	const uint8_t *intelsetup_payload;
	uint32_t intelsetup_size;
	/* True only when the two field offsets below were proven on this
	 * board's payload.  When false the payload is served verbatim and no
	 * field is overridden.
	 */
	bool intelsetup_offsets_proven;
};

/* The selected board's contract, or NULL when no contract is configured. */
const struct grantley_native_contract *grantley_native_contract(void);

/*
 * Check the pinned blob bytes against the contract before the entry point.
 *
 * Returns NULL when the bytes satisfy the contract, otherwise a static string
 * naming the mismatch.  For a BLOB_ECAM contract this reads the module's own
 * ECAM datum and requires the entry-time copy instruction to reference it; for
 * a RUNTIME_TOKEN1 contract there is nothing to read here and the PCD gate
 * already refused earlier.
 *
 * Reads only the image the caller supplies: no MMIO, no PEI service call.
 */
const char *grantley_native_check_blob_ecam(const void *image, size_t size);

#endif /* NORTHBRIDGE_INTEL_GRANTLEY_NATIVE_CONTRACT_H */
