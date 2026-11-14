/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NORTHBRIDGE_INTEL_GRANTLEY_PEI_TRACE_H
#define NORTHBRIDGE_INTEL_GRANTLEY_PEI_TRACE_H

#include <stdint.h>
#include <types.h>

/*
 * The memory initializer is opaque: release builds carry no serial output of
 * their own, so a stall inside it is indistinguishable from a slow training
 * run. Logging every service it calls, together with the address it called
 * from, is the only in-band progress report the port can produce.
 */
enum grantley_pei_slot {
	GRANTLEY_PEI_INSTALL_PPI,
	GRANTLEY_PEI_REINSTALL_PPI,
	GRANTLEY_PEI_LOCATE_PPI,
	GRANTLEY_PEI_NOTIFY_PPI,
	GRANTLEY_PEI_GET_BOOT_MODE,
	GRANTLEY_PEI_SET_BOOT_MODE,
	GRANTLEY_PEI_GET_HOB_LIST,
	GRANTLEY_PEI_CREATE_HOB,
	GRANTLEY_PEI_FFS_FIND_NEXT_VOLUME,
	GRANTLEY_PEI_FFS_FIND_NEXT_FILE,
	GRANTLEY_PEI_FFS_FIND_SECTION_DATA,
	GRANTLEY_PEI_INSTALL_PEI_MEMORY,
	GRANTLEY_PEI_ALLOCATE_PAGES,
	GRANTLEY_PEI_ALLOCATE_POOL,
	GRANTLEY_PEI_COPY_MEM,
	GRANTLEY_PEI_SET_MEM,
	GRANTLEY_PEI_REPORT_STATUS_CODE,
	GRANTLEY_PEI_RESET_SYSTEM,
	GRANTLEY_PEI_SLOT_COUNT,
};

#if CONFIG(GRANTLEY_UNCORE_TRACE)
/*
 * caller is the module's return address; arg0/arg1 are slot-specific and are
 * printed only where they identify the request (PPI GUID, HOB type, sizes).
 */
void grantley_pei_trace(enum grantley_pei_slot slot, uintptr_t caller,
			unsigned long arg0, unsigned long arg1);
#else
static inline void grantley_pei_trace(enum grantley_pei_slot slot, uintptr_t caller,
				      unsigned long arg0, unsigned long arg1)
{
}
#endif

#endif /* NORTHBRIDGE_INTEL_GRANTLEY_PEI_TRACE_H */
