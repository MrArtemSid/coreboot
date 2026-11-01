/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SOC_SYNTACORE_SCR_ADDRESSMAP_H
#define SOC_SYNTACORE_SCR_ADDRESSMAP_H

#include <scr_ddr_base.h>

/*
 * Memory layout addresses. OCRAM base and the reset address are Kconfig
 * symbols because the linker scripts consume them; the HAL build compares
 * them against PLF_OCRAM_BASE and PLF_OCRAM_SIZE. The DDR base is generated
 * from PLF_MEM_BASE at build time (see hal/mmio_hal.c), so the platform
 * memory map has exactly one source. Addresses of on-chip peripheral
 * registers are in soc/mmio.h.
 */
#define SCR_OCRAM_BASE		CONFIG_SCR_OCRAM_BASE
#define SCR_RESET_VECTOR	CONFIG_SCR_RESET_VECTOR

/*
 * The L3 cluster resets into the bootblock, so its load offset follows the
 * configured reset address. L2 resets into the trampoline in the last 256
 * bytes of OCRAM and keeps the bootblock at a fixed offset.
 */
#if CONFIG(SCR_CLUSTER_L3)
#define SCR_OCRAM_BOOTBLOCK_OFFSET	(SCR_RESET_VECTOR - SCR_OCRAM_BASE)
#else
#define SCR_OCRAM_BOOTBLOCK_OFFSET	0x200
#endif

#define SCR_CBFS_BASE		(SCR_DDR_BASE + 0x8000000)
#define SCR_OPENSBI_BASE		(SCR_DDR_BASE + 0x4000000)

#endif
