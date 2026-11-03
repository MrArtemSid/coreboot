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

/*
 * Memory layout in DDR (relative to SCR_DDR_BASE):
 *  +0x00000000 .. +0x00200000 (2 MiB):  OpenSBI (built or preloaded)
 *  +0x00200000 .. +0x02200000 (32 MiB): Payload / U-Boot (preloaded)
 *  +0x02200000 .. +0x02300000 (1 MiB):  FDT / board.dtb (preloaded)
 *  +0x02300000 .. +0x06000000 (61 MiB): Linux Kernel Image (preloaded)
 *  +0x06000000 .. +0x06200000 (2 MiB):  coreboot ramstage (both flows)
 *  +0x06200000 .. +0x06400000 (2 MiB):  Stacks, consoles, mcache (both flows)
 *  +0x08000000 .. +0x09000000 (16 MiB): CBFS flash ROM
 *  +0x30000000 ..                       Linux initrd
 */
#define SCR_OPENSBI_BASE	(SCR_DDR_BASE + 0x0)
#define SCR_OPENSBI_SIZE	0x200000

#define SCR_RAMSTAGE_BASE	(SCR_DDR_BASE + 0x06000000)
#define SCR_CBFS_BASE		(SCR_DDR_BASE + 0x8000000)

/* Preloaded DTB anchor in QEMU */
#define SCR_PRELOADED_DTB_BASE		(SCR_DDR_BASE + 0x2200000)
#define SCR_PRELOADED_DTB_MAX_SIZE	0x100000

#endif
