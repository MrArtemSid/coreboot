/* SPDX-License-Identifier: GPL-2.0-only */

#include <memlayout.h>
#include <arch/header.ld>
#include <soc/addressmap.h>

/*
 * Boot flow mirrors miniboot: bootblock and romstage execute from
 * on-chip SRAM (SCR QEMU jumps to OCRAM base + 0x200 at reset), and
 * everything the early stages reference through PC-relative addressing
 * (stack, preram cbmem console, fmap cache, cbfs mcache) stays in the
 * same OCRAM window - OCRAM and DDR are 16 GiB apart on SCR clusters,
 * far beyond the medany code model's +/-2 GiB reach. DDR holds only
 * what is reached through runtime pointers: ramstage, OpenSBI, the
 * CBFS ROM window, cbmem.
 *
 * Region addresses must be monotonically increasing in this file.
 */

SECTIONS
{
	BOOTBLOCK(SCR_RESET_VECTOR, 128K)
	ROMSTAGE(SCR_OCRAM_BASE + 0x21000, 128K)
	PRERAM_CBMEM_CONSOLE(SCR_OCRAM_BASE + 0x41000, 8K)
	FMAP_CACHE(SCR_OCRAM_BASE + 0x43000, 2K)
	CBFS_MCACHE(SCR_OCRAM_BASE + 0x43800, 10K)
	STACK(SCR_OCRAM_BASE + 0x46000, 8K * CONFIG_MAX_CPUS)

	DRAM_START(SCR_DDR_BASE)
	RAMSTAGE(SCR_RAMSTAGE_BASE, SCR_RAMSTAGE_SIZE)
	/* OpenSBI fw_dynamic is loaded here (== CONFIG_OPENSBI_TEXT_START
	 * and the FW_TEXT_START the blob was linked with). */
	OPENSBI(SCR_OPENSBI_BASE, 1M)

	/* CBFS image: read-only window in DDR (SPI NOR XIP on FPGA,
	 * preloaded coreboot.rom in SCR QEMU). */
	REGION(flash, SCR_CBFS_WINDOW_BASE, CONFIG_ROM_SIZE, 0)
}
