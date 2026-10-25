/* SPDX-License-Identifier: GPL-2.0-only */

#include <memlayout.h>
#include <arch/header.ld>
#include <soc/addressmap.h>

/*
 * L3-family boards (SCR9/SCR7 L3 cluster): the machines enter the
 * firmware at OCRAM + 0x200. The SCR9 L3 cluster has a 2 MiB OCRAM and
 * a separate romstage; the SCR7 L3 cluster has a 64 KiB OCRAM and
 * carries the romstage inside the bootblock (SEPARATE_ROMSTAGE = n).
 * Everything the early stages reach through PC-relative addressing
 * (stack, preram cbmem console, fmap cache, cbfs mcache) stays in the
 * same OCRAM window: OCRAM and DDR are 16 GiB apart on L3 clusters,
 * far beyond the medany code model's +/-2 GiB reach.
 */

#if CONFIG_SCR_OCRAM_SIZE == 0x200000

SECTIONS
{
	BOOTBLOCK(SCR_RESET_VECTOR, 128K)
	ROMSTAGE(SCR_OCRAM_BASE + 0x21000, 128K)
	PRERAM_CBMEM_CONSOLE(SCR_OCRAM_BASE + 0x41000, 8K)
	FMAP_CACHE(SCR_OCRAM_BASE + 0x43000, 2K)
	CBFS_MCACHE(SCR_OCRAM_BASE + 0x43800, 10K)
	/* Previous-stage harts still use their stacks while resuming peers. */
	REGION(bootblock_stack, SCR_OCRAM_BASE + 0x46000, 4K * CONFIG_MAX_CPUS, 4K)
	REGION(romstage_stack, SCR_OCRAM_BASE + 0x46000 + 4K * CONFIG_MAX_CPUS,
	       4K * CONFIG_MAX_CPUS, 4K)
	REGION(ramstage_stack, SCR_OCRAM_BASE + 0x46000 + 8K * CONFIG_MAX_CPUS,
	       4K * CONFIG_MAX_CPUS, 4K)
#if ENV_BOOTBLOCK
	ALIAS_REGION(bootblock_stack, stack)
#elif ENV_SEPARATE_ROMSTAGE
	ALIAS_REGION(romstage_stack, stack)
#else
	ALIAS_REGION(ramstage_stack, stack)
#endif

	DRAM_START(SCR_DDR_BASE)
	RAMSTAGE(SCR_RAMSTAGE_BASE, SCR_RAMSTAGE_SIZE)
	/* OpenSBI fw_dynamic is loaded here (== CONFIG_OPENSBI_TEXT_START
	 * and the FW_TEXT_START the blob was linked with). */
	OPENSBI(SCR_OPENSBI_BASE, 1M)

	/* CBFS image: read-only window in DDR (SPI NOR XIP on FPGA,
	 * preloaded coreboot.rom in SCR QEMU). */
	REGION(flash, SCR_CBFS_WINDOW_BASE, CONFIG_ROM_SIZE, 0)
}

#else /* 64 KiB OCRAM: single early stage */

SECTIONS
{
	BOOTBLOCK(SCR_RESET_VECTOR, 52K)
	PRERAM_CBMEM_CONSOLE(SCR_OCRAM_BASE + 0xd200, 2K)
	FMAP_CACHE(SCR_OCRAM_BASE + 0xda00, 2K)
	CBFS_MCACHE(SCR_OCRAM_BASE + 0xe200, 1K)
	STACK(SCR_OCRAM_BASE + 0xf000, 4K * CONFIG_MAX_CPUS)

	DRAM_START(SCR_DDR_BASE)
	RAMSTAGE(SCR_RAMSTAGE_BASE, SCR_RAMSTAGE_SIZE)
	OPENSBI(SCR_OPENSBI_BASE, 1M)
	REGION(flash, SCR_CBFS_WINDOW_BASE, CONFIG_ROM_SIZE, 0)
}

#endif
