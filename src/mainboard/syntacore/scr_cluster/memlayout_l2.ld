/* SPDX-License-Identifier: GPL-2.0-only */

#include <memlayout.h>
#include <arch/header.ld>
#include <soc/addressmap.h>

/*
 * L2-family boards (SCR9/SCR7/SCR5 L2 cluster): DDR starts at
 * address zero and the 64 KiB OCRAM sits at the very top of the
 * address space (plf.h EXPAND32ADDR(0xffff0000)); the machine reset PC
 * points at the last OCRAM page, where a four-byte jump stub (loaded
 * next to the bootblock, see reset_stub.bin) hands over to _start.
 *
 * From the top of the address space the low DRAM is a small
 * PC-relative offset away, so the bootblock (which carries the
 * romstage, SEPARATE_ROMSTAGE = n) executes from the OCRAM while its
 * stack, preram cbmem console, FMAP cache and CBFS mcache live at the
 * bottom of DDR. The bootblock image must not cross the end of the
 * address space, hence the tight 50 KiB budget.
 */

SECTIONS
{
	DRAM_START(SCR_DDR_BASE)
	/* DDR must already be available at entry, as in the single-hart port. */
	REGION(bootblock_stack, SCR_DDR_BASE, 4K * CONFIG_MAX_CPUS, 4K)
	REGION(ramstage_stack, SCR_DDR_BASE + 4K * CONFIG_MAX_CPUS,
	       4K * CONFIG_MAX_CPUS, 4K)
#if ENV_ROMSTAGE_OR_BEFORE
	ALIAS_REGION(bootblock_stack, stack)
#else
	ALIAS_REGION(ramstage_stack, stack)
#endif
	PRERAM_CBMEM_CONSOLE(SCR_DDR_BASE + 8K * CONFIG_MAX_CPUS, 2K)
	FMAP_CACHE(SCR_DDR_BASE + 8K * CONFIG_MAX_CPUS + 0x800, 2K)
	CBFS_MCACHE(SCR_DDR_BASE + 8K * CONFIG_MAX_CPUS + 0x1000, 1K)
	RAMSTAGE(SCR_DDR_BASE + 0x100000, SCR_RAMSTAGE_SIZE)
	ROMSTAGE(SCR_DDR_BASE + 0x300000, 128K)
	/* OpenSBI fw_dynamic is loaded here (== CONFIG_OPENSBI_TEXT_START
	 * and the FW_TEXT_START the blob was linked with). */
	OPENSBI(SCR_OPENSBI_BASE, 1M)

	/* CBFS image: read-only window in DDR (SPI NOR XIP on FPGA,
	 * preloaded coreboot.rom in SCR QEMU). */
	REGION(flash, SCR_CBFS_WINDOW_BASE, CONFIG_ROM_SIZE, 0)

	BOOTBLOCK(SCR_OCRAM_BASE + 0x200, 50K)

	/* miniboot-style reset vector at the top of the OCRAM: the machine
	 * reset PC lands here; the linker resolves the jump into the
	 * bootblock. Carried inside bootblock.bin (the FMAP BOOTBLOCK
	 * region has room for the span). */
	.reset_vector CONFIG_SCR_RESET_STUB_ADDR : {
		KEEP(*(.reset_vector))
	}
}
