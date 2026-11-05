/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NORTHBRIDGE_INTEL_GRANTLEY_PEI_SERVICES_H
#define NORTHBRIDGE_INTEL_GRANTLEY_PEI_SERVICES_H

#include <stddef.h>
#include <stdint.h>
#include <PiPei.h>
#include <Ppi/CpuIo.h>
#include <Ppi/Pcd.h>
#include <Ppi/PciCfg2.h>
#include <Ppi/ReadOnlyVariable2.h>

#define MAX_GRANTLEY_PPI_DESCRIPTORS 32

/* serialDebugMsgLvl; 1 is SDBG_MIN. */
#define GRANTLEY_RC_SERIAL_SDBG_MIN	1

/* Vendor GUID of the IntelSetup variable. */
#define GRANTLEY_INTELSETUP_VENDOR_GUID { 0xec87d643, 0xeba4, 0x4bb5, \
	{ 0xa1, 0xe5, 0x3f, 0x3e, 0x36, 0xb2, 0x0d, 0xa9 } }

/* InstallPeiMemory() must stay between 1 MiB and 4 GiB. */
#define GRANTLEY_PEI_MEMORY_FLOOR 0x100000ULL
#define GRANTLEY_PEI_MEMORY_LIMIT 0x100000000ULL

struct grantley_uncore_state {
	struct _EFI_PEI_SERVICES services;
	const struct _EFI_PEI_SERVICES *services_ptr;

	/* PPI Registry */
	const EFI_PEI_PPI_DESCRIPTOR * ppi_descriptors[MAX_GRANTLEY_PPI_DESCRIPTORS];
	UINTN ppi_count;
	EFI_PEI_PPI_DESCRIPTOR platform_info_ppi;

	/* IntelSetup default variable served to the PEIM, or NULL. */
	const void *setup;
	size_t setup_size;

	uintptr_t saved_esp;

	/* HOB storage */
	void *hob_list;
	UINTN hob_size;
	UINTN hob_allocated;

	/* Pool memory */
	UINT8 *pool_base;
	UINTN pool_size;
	UINTN pool_allocated;

	/* Boot Mode */
	EFI_BOOT_MODE boot_mode;

	/* Region the module passed to InstallPeiMemory() */
	EFI_PHYSICAL_ADDRESS dram_base;
	UINT64 dram_size;
	BOOLEAN dram_initialized;
	BOOLEAN memory_test_failed;
};

/* Serve this payload as the IntelSetup variable; NULL means none. */
void grantley_pei_set_intelsetup(struct grantley_uncore_state *state,
				 const void *data, size_t size);

/* Update the existing PlatformInfo HOB payload before invoking the PEIM. */
EFI_STATUS grantley_pei_set_board_identity(struct grantley_uncore_state *state,
	uint8_t board_id, uint32_t policy_revision);

struct grantley_memory_map;
EFI_STATUS grantley_pei_memory_map(const struct grantley_uncore_state *state,
	uint32_t tolm, uint32_t tohm_lo, uint32_t tohm_hi,
	struct grantley_memory_map *map);

/* Execute UncoreInitPeim in place at its PE ImageBase. */
EFI_STATUS grantley_run_uncore_peim(const void *pe_base, size_t pe_size,
				    struct grantley_uncore_state *state);

/* The state and both arenas must stay live until the module returns. */
EFI_STATUS grantley_pei_services_init(struct grantley_uncore_state *state,
				    void *pool_buf, UINTN pool_size,
				    void *hob_buf, UINTN hob_size);

#endif /* NORTHBRIDGE_INTEL_GRANTLEY_PEI_SERVICES_H */
