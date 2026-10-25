/* SPDX-License-Identifier: GPL-2.0-only */

#include <memlayout.h>
#include <arch/header.ld>

/*
 * Memory map when booting from the stock Android bootloader (LK):
 *
 * LK loads the whole coreboot ROM (bootblock at offset 0, then FMAP and
 * CBFS, matching the default non-x86 flashmap) to
 * CONFIG_MSM89X7_BOOT_IMAGE_ADDR (0x80080000, the stock LK's arm64
 * kernel load address) and jumps to its first byte, which is the
 * bootblock entry containing an ARM64 kernel image header.
 *
 * All other stages run from DRAM placed above the loaded ROM image.
 */
SECTIONS
{
	DRAM_START(CONFIG_MSM89X7_DDR_START)

	BOOTBLOCK(CONFIG_MSM89X7_BOOT_IMAGE_ADDR, 128K)

	/* Everything below must stay clear of the loaded ROM image,
	 * which spans CONFIG_ROM_SIZE bytes starting at
	 * CONFIG_MSM89X7_BOOT_IMAGE_ADDR. */

	/* Device tree pointer handed over by the primary bootloader in
	 * x0, stored by the bootblock entry in the last bytes of the
	 * loaded image. */
	REGION(lk_dtb, CONFIG_MSM89X7_BOOT_IMAGE_ADDR + CONFIG_ROM_SIZE - 8, 8, 8)

	STACK(CONFIG_MSM89X7_BOOT_IMAGE_ADDR + CONFIG_ROM_SIZE, 64K)
	TIMESTAMP(CONFIG_MSM89X7_BOOT_IMAGE_ADDR + CONFIG_ROM_SIZE + 64K, 1K)
	CBFS_MCACHE(CONFIG_MSM89X7_BOOT_IMAGE_ADDR + CONFIG_ROM_SIZE + 68K, 8K)
	FMAP_CACHE(CONFIG_MSM89X7_BOOT_IMAGE_ADDR + CONFIG_ROM_SIZE + 76K, 2K)
	TTB(CONFIG_MSM89X7_BOOT_IMAGE_ADDR + CONFIG_ROM_SIZE + 80K, 128K)
	ROMSTAGE(CONFIG_MSM89X7_BOOT_IMAGE_ADDR + CONFIG_ROM_SIZE + 208K, 128K)
	PRERAM_CBMEM_CONSOLE(CONFIG_MSM89X7_BOOT_IMAGE_ADDR + CONFIG_ROM_SIZE + 336K, 128K)
	RAMSTAGE(CONFIG_MSM89X7_BOOT_IMAGE_ADDR + CONFIG_ROM_SIZE + 464K, 16M)
	POSTRAM_CBFS_CACHE(CONFIG_MSM89X7_BOOT_IMAGE_ADDR + CONFIG_ROM_SIZE + 16M + 464K, 16M)
}
