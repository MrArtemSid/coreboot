/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NORTHBRIDGE_INTEL_HASWELL_EP_PEI_SERVICES_H
#define NORTHBRIDGE_INTEL_HASWELL_EP_PEI_SERVICES_H

#include <stdint.h>
#include <PiPei.h>
#include <Ppi/CpuIo.h>
#include <Ppi/PciCfg2.h>
#include <Ppi/ReadOnlyVariable2.h>

#define MAX_GRANTLEY_PPI_DESCRIPTORS 32

struct grantley_uncore_state {
	struct _EFI_PEI_SERVICES services;
	const struct _EFI_PEI_SERVICES *services_ptr;

	/* PPI Registry */
	const EFI_PEI_PPI_DESCRIPTOR * ppi_descriptors[MAX_GRANTLEY_PPI_DESCRIPTORS];
	UINTN ppi_count;

	/* Core PPIs */
	EFI_GUID pci_cfg_guid;
	EFI_PEI_PCI_CFG2_PPI pci_cfg;
	EFI_PEI_PPI_DESCRIPTOR pci_cfg_desc;

	EFI_GUID cpu_io_guid;
	EFI_PEI_CPU_IO_PPI cpu_io;
	EFI_PEI_PPI_DESCRIPTOR cpu_io_desc;

	EFI_GUID read_only_var_guid;
	EFI_PEI_READ_ONLY_VARIABLE2_PPI read_only_var;
	EFI_PEI_PPI_DESCRIPTOR read_only_var_desc;

	/* Pointer to backing region device for EFI variable store */
	const struct region_device *var_store_rdev;

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

	/* Boot Mode */
	EFI_BOOT_MODE boot_mode;

	/* Resulting initialized DRAM info */
	EFI_PHYSICAL_ADDRESS dram_base;
	UINT64 dram_size;
	BOOLEAN dram_initialized;
};

/* Global or context accessor for PEI services */
struct grantley_uncore_state *grantley_get_uncore_state(void);

/* Initialise MMCFG base address for MMIO-based PCI config access */
void grantley_pei_set_mmcfg(struct grantley_uncore_state *state, uintptr_t mmcfg_base);

/* Attach backing EFI variable store region device */
void grantley_pei_set_var_store(struct grantley_uncore_state *state,
				const struct region_device *rdev);

/* Execute UncoreInitPeim module */
EFI_STATUS grantley_run_uncore_peim(const void *pe_base, struct grantley_uncore_state *state);

/* Initialize PEI services table and internal storage */
EFI_STATUS grantley_pei_services_init(struct grantley_uncore_state *state,
				    void *pool_buf, UINTN pool_size,
				    void *hob_buf, UINTN hob_size);

#endif /* NORTHBRIDGE_INTEL_HASWELL_EP_PEI_SERVICES_H */
