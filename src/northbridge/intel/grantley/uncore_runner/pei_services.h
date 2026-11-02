/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NORTHBRIDGE_INTEL_GRANTLEY_PEI_SERVICES_H
#define NORTHBRIDGE_INTEL_GRANTLEY_PEI_SERVICES_H

#include <rules.h>
#include <stddef.h>
#include <stdint.h>
#include <PiPei.h>
#include <Ppi/CpuIo.h>
#include <Ppi/PciCfg2.h>
#include <Ppi/ReadOnlyVariable2.h>

#define MAX_GRANTLEY_PPI_DESCRIPTORS 32

/* Stock default of the EFI variable L"IntelSetup" (SYSTEM_CONFIGURATION) as
 * read by UncoreInitPeim: 6091 bytes.  The PEIM consumes offsets >= 0x2D6;
 * the two verified fields this port may override are named below.
 */
#define GRANTLEY_INTELSETUP_SIZE		0x17cb
#define GRANTLEY_INTELSETUP_OFF_DDR_FREQ_LIMIT	0x1248
#define GRANTLEY_INTELSETUP_OFF_ECC_SUPPORT	0x11ff
/* EccSupport enum: 0 = disabled, 1 = enabled, 2 = automatic.
 *
 * The stock IntelSetup payload contains 2 (AUTO).  The blob compares the
 * field with 2 and 1 (0xFFE5D8CD) and enables or disables the ECC bit
 * accordingly, so writing 1 on an explicit ecc_en request follows the
 * blob's code shape; it is NOT a byte-observed "enabled" value in this
 * image.  With no request the stock 2 (AUTO) is kept verbatim.
 */
#define GRANTLEY_INTELSETUP_ECC_DISABLED	0
#define GRANTLEY_INTELSETUP_ECC_ENABLED		1
#define GRANTLEY_INTELSETUP_ECC_AUTO		2

/* Vendor GUID the vendor PEIM passes to GetVariable(L"IntelSetup").  The
 * value is at image 0xFFDCFBF8 and is the EFI variable namespace owner, not
 * an arbitrary store selector: stock GetVariable matches name AND GUID.
 */
#define GRANTLEY_INTELSETUP_VENDOR_GUID { 0xec87d643, 0xeba4, 0x4bb5, \
	{ 0xa1, 0xe5, 0x3f, 0x3e, 0x36, 0xb2, 0x0d, 0xa9 } }

/* IA-32 PEI executes without paging: only memory below 4 GiB is reachable. */
#define GRANTLEY_PEI_MEMORY_LIMIT 0x100000000ULL
/* The first MiB is legacy address space (IVT/BDA/EBDA, option ROMs, VGA);
 * no resource HOB describes it as general purpose RAM on this platform.
 */
#define GRANTLEY_PEI_MEMORY_FLOOR 0x100000ULL

/* Why InstallPeiMemory() cannot be honored by this runner.
 *
 * The PEI core's InstallPeiMemory() implementation
 * only records the region, sets FreePhysicalMemoryTop and raises
 * SwitchStackSignal.  The dispatcher then builds a Stack HOB, moves the HOB
 * heap and the stack into the region, updates the PHIT (EfiMemoryTop/
 * EfiMemoryBottom/EfiFreeMemoryTop/EfiFreeMemoryBottom) and switches stacks
 * (Dispatcher/Dispatcher.c, PeiCheckAndSwitchStack).  This runner has no
 * dispatcher, no stack migration and no PHIT rewrite, so installing a window
 * would hand the PEIM memory the rest of the firmware does not recognize.
 * The region the blob passes is also a runtime HOB field ([hob+0xFF4] /
 * [hob+0xFF8] at both call sites), not a statically known constant.
 */
enum grantley_pei_memory_refusal {
	GRANTLEY_PEI_MEMORY_REGION_OK = 0,
	GRANTLEY_PEI_MEMORY_REFUSED_INVALID,
	GRANTLEY_PEI_MEMORY_REFUSED_UNREACHABLE,
	GRANTLEY_PEI_MEMORY_REFUSED_LIVE_STORAGE,
	GRANTLEY_PEI_MEMORY_REFUSED_SMRAM,
	GRANTLEY_PEI_MEMORY_REFUSED_NO_VENDOR_CONTRACT,
	GRANTLEY_PEI_MEMORY_REFUSED_WINDOW,
	/* The trained TOLM readback is absent or unusable: zero, reserved bits
	 * set (dead config cycle), above the ECAM window, or too small to hold
	 * the SMM reservation. */
	GRANTLEY_PEI_MEMORY_REFUSED_NO_TOLM,
};

struct grantley_uncore_state {
	struct _EFI_PEI_SERVICES services;
	const struct _EFI_PEI_SERVICES *services_ptr;

	/* PPI Registry */
	const EFI_PEI_PPI_DESCRIPTOR * ppi_descriptors[MAX_GRANTLEY_PPI_DESCRIPTORS];
	UINTN ppi_count;
	EFI_PEI_PPI_DESCRIPTOR platform_info_ppi;

	/* Why grantley_run_uncore_peim() refused to enter the module. */
	enum grantley_uncore_stop {
		GRANTLEY_STOP_NONE = 0,
		GRANTLEY_STOP_DEPEX,
		GRANTLEY_STOP_PCD,
		GRANTLEY_STOP_IMAGE,
		GRANTLEY_STOP_HASH,
		/* The pinned blob or its compiled-in MMCFG constant does not
		 * match the board contract. */
		GRANTLEY_STOP_CONTRACT,
	} stop_reason;

	/* InstallPeiMemory() was called and refused; no permanent window was
	 * created and no HOB/stack/PHIT migration happened.  The refusal reason
	 * is kept for the diagnostic report.
	 */
	BOOLEAN memory_refused;
	enum grantley_pei_memory_refusal memory_refusal;

	/* Pointer to backing region device for EFI variable store */
	const struct region_device *var_store_rdev;

	uintptr_t saved_esp;

	/* MMCFG Base configuration */
	uintptr_t mmcfg_base;
	BOOLEAN mmcfg_enabled;

	/* HOB storage */
	void *hob_list;
	UINTN hob_size;
	UINTN hob_allocated;

	/* Pool memory */
	UINT8 *pool_base;
	UINTN pool_size;
	UINTN pool_allocated;

	/* Permanent PEI memory installed by the PEIM, IA-32 addressable window */
	uintptr_t perm_base;
	UINTN perm_size;
	UINTN perm_allocated;

	/* Exclusion window the accepted arena must not overlap (TSEG/IED).
	 * One pair serves both the pre-entry refusal diagnostic
	 * (grantley_pei_set_smram) and the accepted install
	 * (grantley_pei_set_memory_window).  Before the PEIM runs TSEG is not
	 * yet programmed, so a validated window is normally zero; whether the
	 * pair has been resolved at all is tracked separately, because an
	 * unresolved exclusion is never "no exclusion".
	 */
	uint64_t smram_base;
	uint64_t smram_size;
	BOOLEAN exclusion_known;

	/* Highest address the installed claim may reach: the validated backing
	 * limit (TOLM end).  Zero means no window has been pinned yet, and
	 * InstallPeiMemory() then pins one from the trained TOLM readback.
	 */
	UINT64 memory_ceiling;
	/* Highest address the allocator may hand out.  Equal to memory_ceiling
	 * unless a future SMRAM reservation has to be kept out of the arena: the
	 * Uncore PEIM programs the TSEG before InstallPeiMemory() (GetMemoryMap()
	 * trims the main-memory descriptor at the TSEG base), so the arena stops
	 * at that base while the claim itself stays valid against TOLM.
	 */
	UINT64 arena_ceiling;

	/* Set when a claim was refused while no arena was accepted.  Both
	 * allocators then fail closed: the pinned PEIM does not test the
	 * returned status (0xFFE60FFD, 0xFFE61513) and allocates immediately
	 * afterwards (0xFFE6112F), so serving CAR would hand it temporary
	 * memory it believes is permanent.
	 */
	BOOLEAN memory_install_failed;

	/* Boot Mode */
	EFI_BOOT_MODE boot_mode;

	/* Override parameters provided by mainboard / configuration */
	uint16_t max_dram_speed_mts;
	uint8_t tcl;
	uint8_t trcd;
	uint8_t trp;
	uint8_t tras;
	uint8_t command_rate;
	bool ecc_en;

	/* Resulting initialized DRAM info */
	EFI_PHYSICAL_ADDRESS dram_base;
	UINT64 dram_size;
	/* Top of the accepted arena; AllocatePages() carves whole pages
	 * downwards from here, floored at dram_base, the way the stock core
	 * does once PeiMemoryInstalled is set.
	 */
	UINT64 dram_free_top;
	BOOLEAN dram_initialized;
	BOOLEAN memory_test_failed;
};

/* Global or context accessor for PEI services */
#if ENV_SEPARATE_ROMSTAGE || ENV_TEST
struct grantley_uncore_state *grantley_get_uncore_state(void);
#else
static inline struct grantley_uncore_state *grantley_get_uncore_state(void)
{
	return ((void *)0);
}
#endif

/* Initialise MMCFG base address for MMIO-based PCI config access */
EFI_STATUS grantley_pei_set_mmcfg(struct grantley_uncore_state *state, uintptr_t mmcfg_base);

/* Attach backing EFI variable store region device */
void grantley_pei_set_var_store(struct grantley_uncore_state *state,
				const struct region_device *rdev);

/* Update the existing PlatformInfo HOB payload before invoking the PEIM. */
EFI_STATUS grantley_pei_set_board_identity(struct grantley_uncore_state *state,
	uint8_t board_id, uint32_t policy_revision);

/* Configure memory override parameters (or clear to 0 for automatic SPD detection) */
void grantley_pei_set_mem_params(struct grantley_uncore_state *state,
				 uint16_t max_dram_speed_mts,
				 uint8_t tcl, uint8_t trcd, uint8_t trp,
				 uint8_t tras, uint8_t command_rate, bool ecc_en);

struct grantley_memory_map;
EFI_STATUS grantley_pei_memory_map(const struct grantley_uncore_state *state,
	uint32_t tolm, uint32_t tohm_lo, uint32_t tohm_hi,
	struct grantley_memory_map *map);

/* Execute UncoreInitPeim in place.
 *
 * pe_base/pe_size describe the mapped image; the image is verified and then
 * executed only at its own PE ImageBase.  CBFS placement already satisfies
 * that (data_off 0x38 puts the payload at 0xFFDCF9B8); this port has no
 * loader/relocator, so a mapping at another address is refused.
 *
 * Before the entry point the runner also requires the PCD provider to be able
 * to answer Get64(1) with a byte-proven value; otherwise it stops (see
 * stop_reason) instead of entering the module with a guessed base.
 */
EFI_STATUS grantley_run_uncore_peim(const void *pe_base, size_t pe_size,
				    struct grantley_uncore_state *state);

/* PPI GUIDs named by the PEI_DEPEX section of the stock UncoreInitPeim (FFS
 * file at flash 0xDCF918, PEI_DEPEX at 0xDCF930, 0x82 bytes: 7 PUSH, 6 AND,
 * END).  A PI PEI core dispatches the PEIM only when all of them are
 * installed; this runner calls the entry point directly, so it must check the
 * same set itself.
 */
#define GRANTLEY_UNCORE_DEPEX_COUNT 7
extern const EFI_GUID grantley_uncore_depex[GRANTLEY_UNCORE_DEPEX_COUNT];

/* EFI_SUCCESS when every DEPEX PPI is installed; EFI_NOT_STARTED otherwise,
 * with the first absent GUID stored in *missing when it is not NULL.
 */
EFI_STATUS grantley_uncore_deps_check(const struct _EFI_PEI_SERVICES **services,
				      EFI_GUID *missing);

/* Validate the address range only (addressability and overlap with storage
 * this runner is still using).  This is NOT an InstallPeiMemory contract:
 * a clean region returns EFI_SUCCESS here, but the service still refuses it
 * because HOB/stack/PHIT migration is not implemented.  See
 * grantley_pei_memory_contract_refusal().
 */
EFI_STATUS grantley_pei_memory_region_check(const struct grantley_uncore_state *state,
					    uint64_t begin, uint64_t length);

/* Classify a proposed permanent-memory region for the refusal diagnostic.
 * Returns REGION_OK only for a range that passes the address/live-storage/
 * SMRAM checks; any such range is then still refused with
 * GRANTLEY_PEI_MEMORY_REFUSED_NO_VENDOR_CONTRACT because this runner cannot
 * perform the migration the vendor PEI core performs.
 */
enum grantley_pei_memory_refusal
grantley_pei_memory_contract_refusal(const struct grantley_uncore_state *state,
				     uint64_t begin, uint64_t length);

const char *grantley_pei_memory_refusal_string(enum grantley_pei_memory_refusal why);

/* Record the SMRAM/TSEG window so the region check can reject it.  Only a
 * validated window (page aligned, inside the low 4 GiB, size > 0) is kept.
 * There is no caller before the PEIM runs: TSEG is not programmed yet, and
 * this port will not guess where SMRAM is.
 */
void grantley_pei_set_smram(struct grantley_uncore_state *state,
			    uint64_t base, uint64_t size);

/* Pin the DRAM ceiling and the exclusion a claim accepted through
 * InstallPeiMemory() must respect.
 *
 * The stock PEI core does not validate what a PEIM installs: 0xFFDA26FF only
 * stores Begin/Length, computes FreePhysicalMemoryTop and sets
 * PeiMemoryInstalled.  AllocatePages (0xFFDA275E) then carves pages downwards
 * from that top, so a wrong range silently becomes allocatable memory.  This
 * provider validates the claim against the window pinned here.
 *
 * Without a pinned window InstallPeiMemory() keeps its E7 refusal: this runner
 * still cannot perform the vendor core's HOB/stack/PHIT migration, and the
 * region the blob passes is a runtime value (host globals written after MRC),
 * not one that can be proven before entry.  Pinning a window is therefore the
 * single seam a post-MRC caller uses; nothing in this port pins one yet.
 *
 * @param ceiling        Highest address DRAM may reach (MMCFG/ECAM base).
 *                       Must not exceed CONFIG_DCACHE_RAM_BASE.
 * @param reserved_base  Region the arena must not overlap (TSEG/IED), or 0.
 * @param reserved_size  Size of that region.
 * @param reserved_known Whether the platform has actually resolved the pair
 *                       above.  FALSE means "not known yet": the install is
 *                       then refused instead of treating the missing
 *                       exclusion as "there is none".  Pass TRUE with base 0
 *                       and size 0 only to declare that the platform
 *                       positively has no such region.
 */
EFI_STATUS grantley_pei_set_memory_window(struct grantley_uncore_state *state,
					  uint64_t ceiling,
					  EFI_PHYSICAL_ADDRESS reserved_base,
					  uint64_t reserved_size,
					  BOOLEAN reserved_known);


/* The stock IntelSetup default payload extracted from the QD4 BIOS image
 * (see pei_intelsetup_stock.h for image hash, offsets and payload hash).
 */
const uint8_t *grantley_intelsetup_stock(void);

/* Permit (or refuse) serving that QD4-specific payload when the board has no
 * live IntelSetup variable.  The bytes are a QD4 factory default, not a
 * platform-independent structure: another board's VARSTORE may legitimately
 * hold different defaults.  romstage passes CONFIG(GRANTLEY_INTELSETUP_STOCK_QD4);
 * when refused, GetVariable("IntelSetup") answers EFI_NOT_FOUND and the PEIM
 * falls back to its own ZeroMem(0x17CB) + IIO default path.  Host tests start
 * with the payload enabled so the exact bytes stay pinned by a test.
 */
void grantley_intelsetup_allow_stock(bool enable);

/* Initialize PEI services table and internal storage.
 * State and both 8-byte-aligned, non-overlapping arenas must remain live until
 * the PEIM returns. Firmware callers keep them in static CAR below its stack.
 */
EFI_STATUS grantley_pei_services_init(struct grantley_uncore_state *state,
				    void *pool_buf, UINTN pool_size,
				    void *hob_buf, UINTN hob_size);

#endif /* NORTHBRIDGE_INTEL_GRANTLEY_PEI_SERVICES_H */
