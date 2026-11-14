/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <stdint.h>
#include "pei_trace.h"

#if CONFIG(GRANTLEY_UNCORE_TRACE)

static const char *const slot_name[GRANTLEY_PEI_SLOT_COUNT] = {
	[GRANTLEY_PEI_INSTALL_PPI]		= "InstallPpi",
	[GRANTLEY_PEI_REINSTALL_PPI]		= "ReInstallPpi",
	[GRANTLEY_PEI_LOCATE_PPI]		= "LocatePpi",
	[GRANTLEY_PEI_NOTIFY_PPI]		= "NotifyPpi",
	[GRANTLEY_PEI_GET_BOOT_MODE]		= "GetBootMode",
	[GRANTLEY_PEI_SET_BOOT_MODE]		= "SetBootMode",
	[GRANTLEY_PEI_GET_HOB_LIST]		= "GetHobList",
	[GRANTLEY_PEI_CREATE_HOB]		= "CreateHob",
	[GRANTLEY_PEI_FFS_FIND_NEXT_VOLUME]	= "FfsFindNextVolume",
	[GRANTLEY_PEI_FFS_FIND_NEXT_FILE]	= "FfsFindNextFile",
	[GRANTLEY_PEI_FFS_FIND_SECTION_DATA]	= "FfsFindSectionData",
	[GRANTLEY_PEI_INSTALL_PEI_MEMORY]	= "InstallPeiMemory",
	[GRANTLEY_PEI_ALLOCATE_PAGES]		= "AllocatePages",
	[GRANTLEY_PEI_ALLOCATE_POOL]		= "AllocatePool",
	[GRANTLEY_PEI_COPY_MEM]			= "CopyMem",
	[GRANTLEY_PEI_SET_MEM]			= "SetMem",
	[GRANTLEY_PEI_REPORT_STATUS_CODE]	= "ReportStatusCode",
	[GRANTLEY_PEI_RESET_SYSTEM]		= "ResetSystem",
};

/*
 * The module copies large structures through CopyMem in training loops, so the
 * per-slot budgets are what keep a trace readable instead of turning the
 * console into a bottleneck that looks like a stall. Exhausting a budget only
 * stops that slot's messages.
 */
static const uint16_t slot_budget[GRANTLEY_PEI_SLOT_COUNT] = {
	[GRANTLEY_PEI_INSTALL_PPI]		= 8,
	[GRANTLEY_PEI_REINSTALL_PPI]		= 4,
	[GRANTLEY_PEI_LOCATE_PPI]		= 48,
	[GRANTLEY_PEI_NOTIFY_PPI]		= 4,
	[GRANTLEY_PEI_GET_BOOT_MODE]		= 8,
	[GRANTLEY_PEI_SET_BOOT_MODE]		= 8,
	[GRANTLEY_PEI_GET_HOB_LIST]		= 4,
	[GRANTLEY_PEI_CREATE_HOB]		= 32,
	[GRANTLEY_PEI_FFS_FIND_NEXT_VOLUME]	= 4,
	[GRANTLEY_PEI_FFS_FIND_NEXT_FILE]	= 8,
	[GRANTLEY_PEI_FFS_FIND_SECTION_DATA]	= 4,
	[GRANTLEY_PEI_INSTALL_PEI_MEMORY]	= 4,
	[GRANTLEY_PEI_ALLOCATE_PAGES]		= 8,
	[GRANTLEY_PEI_ALLOCATE_POOL]		= 16,
	[GRANTLEY_PEI_COPY_MEM]			= 12,
	[GRANTLEY_PEI_SET_MEM]			= 8,
	[GRANTLEY_PEI_REPORT_STATUS_CODE]	= 8,
	[GRANTLEY_PEI_RESET_SYSTEM]		= 4,
};

static uint16_t slot_printed[GRANTLEY_PEI_SLOT_COUNT];

void grantley_pei_trace(enum grantley_pei_slot slot, uintptr_t caller,
			unsigned long arg0, unsigned long arg1)
{
	if (slot >= GRANTLEY_PEI_SLOT_COUNT || !slot_name[slot])
		return;
	if (slot_printed[slot] >= slot_budget[slot])
		return;
	slot_printed[slot]++;

	switch (slot) {
	case GRANTLEY_PEI_LOCATE_PPI:
		printk(BIOS_INFO, "PEIM %s from %p: guid %08lx-%08lx\n",
		       slot_name[slot], (void *)caller, arg0, arg1);
		break;
	case GRANTLEY_PEI_CREATE_HOB:
		printk(BIOS_INFO, "PEIM %s from %p: type %lu length %lu\n",
		       slot_name[slot], (void *)caller, arg0, arg1);
		break;
	case GRANTLEY_PEI_INSTALL_PEI_MEMORY:
		printk(BIOS_INFO, "PEIM %s from %p: begin 0x%lx size 0x%lx\n",
		       slot_name[slot], (void *)caller, arg0, arg1);
		break;
	default:
		printk(BIOS_INFO, "PEIM %s from %p\n", slot_name[slot], (void *)caller);
		break;
	}
}

#endif
