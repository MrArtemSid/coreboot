/* SPDX-License-Identifier: GPL-2.0-only */

#define __SIMPLE_DEVICE__

#include <console/console.h>
#include <cpu/intel/em64t101_save_state.h>
#include <cpu/intel/smm_reloc.h>
#include <cpu/x86/msr.h>
#include <cpu/x86/mtrr.h>
#include <cpu/x86/smm.h>
#include <device/pci_def.h>
#include <device/pci_ops.h>
#include <string.h>
#include "grantley.h"

/* Socket 0 uncore bus, established before memory initialization. */
#define IIO_DEV PCI_DEV(0, 5, 0)
#define SAD_DEV PCI_DEV(0xff, 12, 6)
#define GRANTLEY_MSR_LT_CONTROL 0x2e7
#define UBOX_DFX_DEV PCI_DEV(0xff, 16, 6)
#define SAD_SMRAMC 0x4c
#define SMRAM_ENABLE (1 << 3)
#define SMRAM_LOCK (1 << 4)
#define SMRAM_OPEN (1 << 6)
#define SMM_FEATURE_CONTROL 0x58
#define SMM_SAVE_STATE_MSR (1 << 1)

static msr_t smrr_base;
static msr_t smrr_mask;
static uintptr_t ied_base;

void grantley_get_smm_info(uintptr_t *base, size_t *size, size_t *save_state_size)
{
	uintptr_t tseg_base;
	size_t tseg_size, ied_size;

	/* This API must report the TSEG decoded by hardware, not a guessed carveout. */
	smm_region(&tseg_base, &tseg_size);
	/* IIO protects this entire interval, including IED, from DMA. */
	const uint32_t hw_base = pci_s_read_config32(IIO_DEV, 0xa8);
	const uint32_t hw_limit = pci_s_read_config32(IIO_DEV, 0xac);
	const uint64_t hw_end = (uint64_t)(hw_limit & 0xfff00000) + MiB;
	if (hw_base == UINT32_MAX || hw_limit == UINT32_MAX ||
	    (hw_base & 0xfff00000) != tseg_base ||
	    (uint64_t)tseg_base + tseg_size > hw_end)
		die("Grantley: SMRAM handoff disagrees with IIO TSEG\n");
	if (!grantley_smrr_values(tseg_base, tseg_size, &smrr_base.lo, &smrr_mask.lo))
		die("Grantley: invalid hardware TSEG for SMRR\n");
	if (!(rdmsr(MTRR_CAP_MSR).lo & MTRR_CAP_SMRR))
		die("Grantley: SMRR required for SMM\n");
	if (pci_s_read_config16(SAD_DEV, PCI_VENDOR_ID) != 0x8086 ||
	    pci_s_read_config16(UBOX_DFX_DEV, PCI_VENDOR_ID) != 0x8086)
		die("Grantley: SMM uncore registers unavailable\n");

	/* Do not confuse the server CSR with a client SMM feature-control MSR.
	 * We only implement memory save states. Never relocate against stale memory
	 * if a previous stage enabled MSR save states for this package. */
	const uint32_t feature = pci_s_read_config32(UBOX_DFX_DEV, SMM_FEATURE_CONTROL);
	if (feature == UINT32_MAX || (feature & SMM_SAVE_STATE_MSR))
		die("Grantley: memory SMM save states required\n");

	if (smm_subregion(SMM_SUBREGION_HANDLER, base, size) ||
	    smm_subregion(SMM_SUBREGION_CHIPSET, &ied_base, &ied_size))
		die("Grantley: SMM subregion unavailable\n");

	/* The chipset IED region is distinct from the handler and stage cache. */
	if (ied_size) {
		const struct ied_header header = {
			.signature = "INTEL RSVD",
			.size = ied_size,
		};
		if (ied_size < 2 * MiB)
			die("Grantley: IED region too small\n");
		memcpy((void *)ied_base, &header, sizeof(header));
		memset((void *)(ied_base + MiB), 0, 32 * KiB);
	} else {
		ied_base = UINT32_MAX;
	}
	*save_state_size = sizeof(em64t101_smm_state_save_area_t);
}

void grantley_smm_initialize(void)
{
	smm_southbridge_clear_state();
}

void grantley_smm_relocation_handler(int cpu, uintptr_t current_smbase,
				     uintptr_t new_smbase)
{
	em64t101_smm_state_save_area_t *state =
		(void *)(current_smbase + SMM_DEFAULT_SIZE - sizeof(*state));

	/* The common MP layer serializes entry into the default SMBASE window. */
	state->smbase = new_smbase;
	state->iedbase = ied_base;
	if (!(rdmsr(MTRR_CAP_MSR).lo & MTRR_CAP_SMRR))
		die("Grantley: AP lacks SMRR\n");
	wrmsr(SMRR_PHYSBASE_MSR, smrr_base);
	wrmsr(SMRR_PHYSMASK_MSR, smrr_mask);
	if (rdmsr(SMRR_PHYSBASE_MSR).raw != smrr_base.raw ||
	    rdmsr(SMRR_PHYSMASK_MSR).raw != smrr_mask.raw)
		die("Grantley: failed to program SMRR\n");
}

void grantley_cpu_finalize(struct device *dev)
{
	/* Lock trusted IIO memory-map registers after PCI resource assignment.
	 * This is package scoped; the current port supports socket 0 only. */
	msr_t control = rdmsr(GRANTLEY_MSR_LT_CONTROL);
	control.lo |= 1;
	wrmsr(GRANTLEY_MSR_LT_CONTROL, control);
	if (!(rdmsr(GRANTLEY_MSR_LT_CONTROL).lo & 1))
		die("Grantley: failed to lock memory configuration\n");
}

void grantley_smm_lock(void)
{
	/* Use the server SAD SMRAM control, never the client host bridge at 00:00.0. */
	uint8_t control = pci_s_read_config8(SAD_DEV, SAD_SMRAMC);
	control = (control & ~SMRAM_OPEN) | SMRAM_ENABLE | SMRAM_LOCK;
	pci_s_write_config8(SAD_DEV, SAD_SMRAMC, control);
	const uint8_t readback = pci_s_read_config8(SAD_DEV, SAD_SMRAMC);
	if ((readback & (SMRAM_OPEN | SMRAM_ENABLE | SMRAM_LOCK)) !=
	    (SMRAM_ENABLE | SMRAM_LOCK))
		die("Grantley: failed to close and lock SMRAM\n");

	/* Only allow normal SMIs after all CPUs have relocated and SMRAM is locked. */
	global_smi_enable();
}
