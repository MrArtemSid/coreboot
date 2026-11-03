/* SPDX-License-Identifier: GPL-2.0-only */

#include <memlayout.h>
#include <soc/addressmap.h>
#include <arch/header.ld>

SECTIONS
{
/* Regions are placed in ascending address order. */
#if CONFIG(SCR_BOOT_HAL) && CONFIG(SCR_CLUSTER_L3)
#if CONFIG(SCR_OCRAM_512K)
#include "hal/ocram_512k.ld"
#else
#include "hal/ocram_64k.ld"
#endif
#include "hal/dram.ld"
#elif CONFIG(SCR_BOOT_HAL)
#include "hal/dram.ld"
#include "hal/ocram_64k.ld"
#else
#include "miniboot/memlayout.ld"
#endif
#if CONFIG(SCR_BOOT_HAL)
	/*
	 * SCR_RESET_VECTOR is the address the cluster resets to. On L2 the
	 * reset trampoline sits in the last 256 bytes of OCRAM; on L3 the
	 * bootblock starts at that address, so the configured value also sets
	 * the bootblock load offset and has to leave room for it inside the
	 * pre-RAM OCRAM budget, whose end the layout above bounds.
	 */
#if CONFIG(SCR_CLUSTER_L2)
	_ = ASSERT(SCR_RESET_VECTOR == SCR_OCRAM_BASE + (CONFIG_SCR_OCRAM_SIZE - 0x100),
		   "L2 reset vector must be the last 256 bytes of OCRAM");
#else
	_ = ASSERT(SCR_RESET_VECTOR >= SCR_OCRAM_BASE,
		   "L3 reset vector lies below OCRAM");
	_ = ASSERT(SCR_RESET_VECTOR < SCR_OCRAM_BASE + CONFIG_SCR_OCRAM_SIZE,
		   "L3 reset vector lies outside OCRAM");
	_ = ASSERT(_ebootblock > _bootblock,
		   "L3 reset vector leaves no room for the bootblock");
#endif
#endif
}
