/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <device/pci_ops.h>
#include <southbridge/intel/lynxpoint/pch.h>
#include <southbridge/intel/lynxpoint/me.h>
#undef NULL
#include "pei_services.h"

/*
 * The memory initializer sizes the ME UMA, carves it out of the memory map,
 * programs the IIO ME range and sends DRAM Init Done through this interface.
 * Each service maps onto the common Lynx Point ME helpers.
 *
 * The initializer treats a missing interface as a failed previous boot and
 * always retrains memory, so the interface is installed even without the
 * handoff; it then reports no UMA and leaves the ME alone.
 */
struct grantley_me_uma_ppi {
	UINT32 (EFIAPI *send_uma_size)(EFI_PEI_SERVICES **services, void *ffs_header);
	EFI_STATUS (EFIAPI *cpu_replacement_check)(EFI_PEI_SERVICES **services,
		void *ffs_header, UINT8 *force_full_training, UINT8 *next_step);
	EFI_STATUS (EFIAPI *config_did_reg)(EFI_PEI_SERVICES **services,
		void *ffs_header, UINT8 init_stat, UINT8 *next_step);
	EFI_STATUS (EFIAPI *handle_bios_action)(EFI_PEI_SERVICES **services,
		UINT8 bios_action);
	BOOLEAN (EFIAPI *uma_enabled)(EFI_PEI_SERVICES **services);
	EFI_STATUS (EFIAPI *host_reset_warning)(EFI_PEI_SERVICES **services,
		void *ffs_header);
};

/* BIOS action codes passed back to the memory initializer. */
#define ME_BIOS_ACTION_NONE		0
#define ME_BIOS_ACTION_CONTINUE		7

/* DRAM Init Done status byte: bit 7 requests Rapid Start, unused here. */
#define ME_DID_STATUS_MASK		0x0f

/*
 * An ME that reports an error or runs in debug mode does not use the UMA
 * and never acknowledges DRAM Init Done, so leave it alone.
 */
static bool me_uma_usable(void)
{
	if (!CONFIG(GRANTLEY_ME_UMA))
		return false;

	union me_hfs hfs = { .raw = pci_read_config32(PCH_ME_DEV, PCI_ME_HFS) };

	if (hfs.error_code != ME_HFS_ERROR_NONE ||
	    hfs.operation_mode == ME_HFS_MODE_DEBUG) {
		printk(BIOS_WARNING, "ME: error %u, mode %u, not using UMA\n",
		       hfs.error_code, hfs.operation_mode);
		return false;
	}
	return true;
}

static UINT32 EFIAPI me_send_uma_size(EFI_PEI_SERVICES **services, void *ffs_header)
{
	if (!CONFIG(GRANTLEY_ME_UMA))
		return 0;

	intel_early_me_status();

	if (!me_uma_usable() || intel_early_me_init())
		return 0;

	return intel_early_me_uma_size();
}

static EFI_STATUS EFIAPI me_cpu_replacement_check(EFI_PEI_SERVICES **services,
	void *ffs_header, UINT8 *force_full_training, UINT8 *next_step)
{
	if (!force_full_training || !next_step)
		return EFI_INVALID_PARAMETER;

	*next_step = ME_BIOS_ACTION_NONE;
	if (CONFIG(GRANTLEY_ME_UMA) && intel_early_me_cpu_replacement_check())
		*force_full_training = 1;

	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI me_config_did_reg(EFI_PEI_SERVICES **services,
	void *ffs_header, UINT8 init_stat, UINT8 *next_step)
{
	if (!next_step)
		return EFI_INVALID_PARAMETER;

	*next_step = ME_BIOS_ACTION_NONE;
	if (!me_uma_usable())
		return EFI_SUCCESS;

	/* Any reset the ME requests is performed before this returns. */
	const int ret = intel_early_me_init_done(init_stat & ME_DID_STATUS_MASK);
	intel_early_me_status();

	*next_step = ret ? ME_BIOS_ACTION_NONE : ME_BIOS_ACTION_CONTINUE;
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI me_handle_bios_action(EFI_PEI_SERVICES **services,
					       UINT8 bios_action)
{
	return EFI_SUCCESS;
}

static BOOLEAN EFIAPI me_uma_enabled(EFI_PEI_SERVICES **services)
{
	return 1;
}

/* Only needed on S3 and S4 resume, which this platform does not support. */
static EFI_STATUS EFIAPI me_host_reset_warning(EFI_PEI_SERVICES **services,
					       void *ffs_header)
{
	return EFI_SUCCESS;
}

static struct grantley_me_uma_ppi me_uma_ppi = {
	.send_uma_size		= me_send_uma_size,
	.cpu_replacement_check	= me_cpu_replacement_check,
	.config_did_reg		= me_config_did_reg,
	.handle_bios_action	= me_handle_bios_action,
	.uma_enabled		= me_uma_enabled,
	.host_reset_warning	= me_host_reset_warning,
};

static EFI_GUID me_uma_ppi_guid = {
	0x8c376010, 0x2400, 0x4d7d,
	{ 0xb4, 0x7b, 0x9d, 0x85, 0x1d, 0xf3, 0xc9, 0xd1 }
};

const EFI_PEI_PPI_DESCRIPTOR grantley_me_uma_ppi_desc = {
	.Flags	= EFI_PEI_PPI_DESCRIPTOR_PPI | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST,
	.Guid	= &me_uma_ppi_guid,
	.Ppi	= &me_uma_ppi,
};
