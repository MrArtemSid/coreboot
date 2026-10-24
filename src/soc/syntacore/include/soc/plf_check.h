/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __SOC_SYNTACORE_PLF_CHECK_H__
#define __SOC_SYNTACORE_PLF_CHECK_H__

/* Compile-time cross-check between the coreboot Kconfig memory anchors
 * and the SCR-HAL platform headers. If a SCR-HAL update changes a
 * platform memory map, the build fails here until the corresponding
 * Kconfig default is updated - platform changes still land with a
 * single commit, they just cannot pass unnoticed. */

#include <stdint.h>
#include <static.h>

/*
 * coreboot preprocesses this file's includer (addressmap.h path is only
 * used from C below, so the compiler always defines __riscv_xlen).
 */
#include "plf.h"

#define SCR_PLF_EQ(a, b) \
	_Static_assert((uintptr_t)(a) == (uintptr_t)(b), \
		       "Kconfig memory anchor drifted from SCR-HAL plf.h: " #a " != " #b)

SCR_PLF_EQ(CONFIG_SCR_DDR_BASE, PLF_MEM_BASE);
/* L2-family platforms sign-extend their 32-bit OCRAM address to 64 bit
 * in plf.h (EXPAND32ADDR); coreboot keeps the 32-bit view. */
SCR_PLF_EQ(CONFIG_SCR_OCRAM_BASE, ((uint64_t)PLF_OCRAM_BASE & 0xffffffff));
SCR_PLF_EQ(CONFIG_SCR_OCRAM_SIZE, PLF_OCRAM_SIZE);

#endif /* __SOC_SYNTACORE_PLF_CHECK_H__ */
