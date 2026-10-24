/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __SOC_SYNTACORE_ADDRESSMAP_H__
#define __SOC_SYNTACORE_ADDRESSMAP_H__

/*
 * The platform memory map comes from two places that must agree:
 *
 *  1. SCR-HAL platform headers (3rdparty/scr-hal/platform/<plat>/plf.h)
 *     are the single source of truth for everything consumed by C code
 *     (UART and MTIMER bases, MMCFG, memory attributes).
 *
 * 2. The Kconfig anchors below (SCR_DDR_BASE, SCR_OCRAM_*) drive the
 *     stage linker scripts, because GNU ld scripts cannot parse the
 *     UL-suffixed constants used by plf.h.
 *
 * include/soc/plf_check.h statically verifies that both views match,
 * so a SCR-HAL platform update cannot silently drift from coreboot.
 */

#include <kconfig.h>

#define SCR_DDR_BASE		CONFIG_SCR_DDR_BASE
#define SCR_OCRAM_BASE		CONFIG_SCR_OCRAM_BASE
#define SCR_OCRAM_SIZE		CONFIG_SCR_OCRAM_SIZE

/* QEMU SCR machines start executing at OCRAM base + 0x200 (see the
 * SCR QEMU boot flow; on FPGA the boot ROM jumps into OCRAM similarly). */
#define SCR_RESET_VECTOR	(SCR_OCRAM_BASE + 0x200)

/*
 * coreboot DRAM layout (offsets from SCR_DDR_BASE):
 *   +0x0000000  ramstage (2M)
 *   +0x400000   OpenSBI fw_dynamic (1M, == CONFIG_OPENSBI_TEXT_START)
 *   +0x8000000  CBFS ROM window (read-only copy of coreboot.rom)
 * The early stages (bootblock, romstage) execute from OCRAM like
 * miniboot does; cbmem grows down from SCR_CBMEM_TOP.
 */
#define SCR_RAMSTAGE_OFFSET	0x0000000
#define SCR_RAMSTAGE_SIZE	(2 * 1024 * 1024)
#define SCR_OPENSBI_OFFSET	0x4000000
#define SCR_CBFS_WINDOW_OFFSET	0x8000000

/* Payload staging area reported to the OS: keep the first 512 MiB of DDR
 * for the boot flow (payload, kernel, initrd), cbmem at the top of it. */
#define SCR_RAM_TOP_OFFSET	(512 * 1024 * 1024)

#define SCR_RAMSTAGE_BASE	(SCR_DDR_BASE + SCR_RAMSTAGE_OFFSET)
#define SCR_OPENSBI_BASE	(SCR_DDR_BASE + SCR_OPENSBI_OFFSET)
#define SCR_CBFS_WINDOW_BASE	(SCR_DDR_BASE + SCR_CBFS_WINDOW_OFFSET)
#define SCR_CBMEM_TOP		(SCR_DDR_BASE + SCR_RAM_TOP_OFFSET)

#endif /* __SOC_SYNTACORE_ADDRESSMAP_H__ */
