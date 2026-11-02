/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NORTHBRIDGE_INTEL_GRANTLEY_PEI_PCD_H
#define NORTHBRIDGE_INTEL_GRANTLEY_PEI_PCD_H

#include <stdbool.h>
#include <stdint.h>
#include <Uefi.h>
#include <Ppi/Pcd.h>
#include <Ppi/PiPcd.h>

/*
 * PCD provider for UncoreInitPeim.
 *
 * The stock PEIM depends on two different PPIs and they are not the same
 * interface:
 *
 *   01F34D25-4DE2-23AD-3FF3-36353FF323F1  gEfiPeiPcdPpiGuid, PI 1.2
 *                                         EFI_PEI_PCD_PPI (EX, Guid+Token).
 *                                         Named in the module's PEI_DEPEX
 *                                         only; the image never references it.
 *   06E81C58-4AD7-44BC-8390-F10265F72480  native PCD_PPI (Ppi/Pcd.h).
 *                                         Located at 0xFFDE90D1 and called
 *                                         unconditionally at module entry:
 *                                         0xFFDE8F91 `push 1` /
 *                                         0xFFDE8F93 `call [eax+0x10]`
 *                                         (Get64), value in EDX:EAX.
 *
 * Both are value-returning APIs: no This, no EFI_STATUS, no OUT parameter for
 * the getters.  A getter therefore has no way to report "no such token".  This
 * provider records a sticky fault and returns 0, but that is NOT fail-closed:
 * the fault becomes visible only after the call, i.e. after a blob would
 * already have consumed the 0.  The runtime guarantee is narrower and rests on
 * two facts: (1) the pinned blob's complete PCD call set is Get64(1) at three
 * sites and nothing else, and (2) the runner refuses entry before the call
 * unless token 1 has the proven value.  Unknown tokens cannot be turned into
 * an error by a value-returning ABI; they are simply not claimed as supported.
 *
 * Token 1 is the MMCFG / PCIe ECAM base (256 MiB window 0x80000000-
 * 0x8FFFFFFF), NOT a CSR base.  The consumer writes through it: base+0xF8040
 * and base+0xF8044 are PCI config 00:1F.0 offsets 0x40 (PMBASE) and 0x44
 * (ACPI_CNTL), and base+(bus<<20)+0xF308C decodes as ECAM.  The stock database
 * at PcdPeim file offset 0xDCB2D4 answers exactly 0x80000000 (slot 0xDCB314,
 * bytes 00 00 00 80 00 00 00 00, proven in reports/reverse-pcd-value.md).
 *
 * Because every 4 KiB-aligned MMIO constant passes an alignment test, the
 * value is accepted only when it EQUALS the platform MMCFG base the port
 * programs.  The earlier 0x40100000 inference would have made the PEIM write
 * 0x0808 into uncore CSR space instead of enabling ACPI decode.
 *
 * Tokens 2 and 4 are byte-determined in the same database (0x1C200 and
 * 0xAA33AA22AAAA0000).  Tokens 3, 6 and 8 point into the five zero-fill bytes
 * beyond the copied database and are NOT determined; their getters return 0
 * with a latched fault rather than a fabricated value.  That is diagnostic
 * honesty, not a runtime error channel.
 */
#define GRANTLEY_PCD_TOKEN_MMCFG_BASE		1
#define GRANTLEY_PCD_TOKEN_BAUD			2
#define GRANTLEY_PCD_TOKEN_4			4

#define GRANTLEY_PCD_TOKEN_BAUD_VALUE		0x1c200ULL
#define GRANTLEY_PCD_TOKEN_4_VALUE		0xaa33aa22aaaa0000ULL

/* Highest offset the PEIM adds to the base: +0xF8040 (word), +0xF8044 (byte),
 * +0xF8048 (word).  The window must stay below the 4 GiB linear boundary.
 */
#define GRANTLEY_PCD_MMCFG_WINDOW_SIZE		0xf8050ULL

enum grantley_pcd_fault {
	GRANTLEY_PCD_OK = 0,
	/* Token 1 was asked for but no value was supplied. */
	GRANTLEY_PCD_FAULT_MISSING_VALUE,
	/* grantley_pcd_set_mmcfg_base() rejected the value it was given. */
	GRANTLEY_PCD_FAULT_INVALID_VALUE,
	/* A token outside this build's database (3, 6, 8, ...). */
	GRANTLEY_PCD_FAULT_UNKNOWN_TOKEN,
	/* EX lookup: this port defines no PI PCD token space. */
	GRANTLEY_PCD_FAULT_UNKNOWN_TOKEN_SPACE,
	/* A caller tried to write or enumerate the database. */
	GRANTLEY_PCD_FAULT_WRITE_UNSUPPORTED,
};

/* Forget the configured value and the recorded fault. */
void grantley_pcd_reset(void);

/* Supply the PCD token 1 value.
 *
 * Accepted only when the value equals platform_mmcfg_base (the base the port
 * programs into the chipset), is non-zero, 4 KiB aligned and keeps the 0xF8050
 * byte window below 4 GiB.
 * Returns 0 on success, -1 when the value was rejected (fault recorded).
 */
int grantley_pcd_set_mmcfg_base(uint64_t base, uint64_t platform_mmcfg_base);

/* Configured value (0 when none). */
uint64_t grantley_pcd_mmcfg_base(void);

/* Whether this board's contract requires Get64(1) to be answerable.
 *
 * Default true, which is the QD4 contract: the module calls Get64(1) at entry
 * and the value must be supplied before entry.  A board whose pinned build
 * does not call the getter (its MMCFG base is a compiled-in constant checked
 * against the blob bytes instead) calls this with false so an unrelated
 * token-1 gate cannot refuse an otherwise proven entry.  The PPI itself stays
 * installed either way because the PEI_DEPEX names it.
 */
void grantley_pcd_set_token1_required(bool required);

/* NULL when the provider can answer the PEIM's Get64(1); otherwise a static
 * description of the reason the entry must not happen.
 *
 * Only meaningful BEFORE the entry point.  A fault latched after entry cannot
 * retroactively stop the blob; see the header comment.
 */
const char *grantley_pcd_blocking_reason(void);

enum grantley_pcd_fault grantley_pcd_fault(void);
uint32_t grantley_pcd_fault_token(void);
const char *grantley_pcd_fault_string(enum grantley_pcd_fault fault);

/* The two PPI interfaces installed by the runner.  Get64(1) is slot 0x10 of
 * grantley_pcd_ppi; the EFI_PEI_PCD_PPI table takes a token space GUID.
 */
/* Interface tables are only read; the PPI descriptor interface field is a
 * non-const void *, so the objects themselves cannot be const-qualified.
 */
extern PCD_PPI grantley_pcd_ppi;
extern EFI_PEI_PCD_PPI grantley_efi_pei_pcd_ppi;

#endif /* NORTHBRIDGE_INTEL_GRANTLEY_PEI_PCD_H */
