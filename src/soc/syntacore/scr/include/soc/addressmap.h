/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SOC_SYNTACORE_SCR_ADDRESSMAP_H
#define SOC_SYNTACORE_SCR_ADDRESSMAP_H

#include <scr_ddr_base.h>

/*
 * The DDR base is generated from PLF_MEM_BASE at build time (see
 * hal/mmio_hal.c), so the platform memory map has exactly one source.
 * Addresses of on-chip peripheral registers are in soc/mmio.h.
 */

#define SCR_CBFS_BASE		(SCR_DDR_BASE + 0x8000000)
#define SCR_OPENSBI_BASE		(SCR_DDR_BASE + 0x4000000)

#endif
