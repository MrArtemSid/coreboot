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
}
