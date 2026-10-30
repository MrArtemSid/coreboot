/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SOC_SYNTACORE_SCR_ADDRESSMAP_H
#define SOC_SYNTACORE_SCR_ADDRESSMAP_H

#if CONFIG(SCR_CLUSTER_L3)
#define SCR_OCRAM_BASE		0x02000000
#define SCR_DDR_BASE		0x400000000
#define SCR_UART0_BASE		0x1e140000
#define SCR_SYSCLK_MHZ		0x0c001000
#define SCR_MTIMER_BASE		0x04010000
#define SCR_PLIC_BASE		0x06000000
#else
#define SCR_OCRAM_BASE		0xffffffffffff0000
#define SCR_DDR_BASE		0x0
#define SCR_UART0_BASE		0xffffffffff010000
#define SCR_SYSCLK_MHZ		0xffffffffff001000
#define SCR_MTIMER_BASE		0xfffffffff0040000
#define SCR_PLIC_BASE		0xfffffffffe000000
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
