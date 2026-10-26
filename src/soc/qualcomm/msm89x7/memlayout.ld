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

	/* The handoff DTB appended behind the image (boot.img
	 * --append-dtb) lands at BOOT_IMAGE_ADDR + ROM_SIZE and is passed
	 * through scm_elexec_call; the EL3 firmware appears to guard the
	 * surrounding block (observed bus hangs on any access to
	 * 0x81080000..0x81100000). Keep every coreboot buffer well clear:
	 * explicit hex offsets, 32 MB and more above the image end. */
	STACK(CONFIG_MSM89X7_BOOT_IMAGE_ADDR + 0x2200000, 0x10000)
	TIMESTAMP(CONFIG_MSM89X7_BOOT_IMAGE_ADDR + 0x2210000, 0x400)
	CBFS_MCACHE(CONFIG_MSM89X7_BOOT_IMAGE_ADDR + 0x2211000, 0x2000)
	FMAP_CACHE(CONFIG_MSM89X7_BOOT_IMAGE_ADDR + 0x2213000, 0x800)
	TTB(CONFIG_MSM89X7_BOOT_IMAGE_ADDR + 0x2214000, 0x20000)
	ROMSTAGE(CONFIG_MSM89X7_BOOT_IMAGE_ADDR + 0x2234000, 0x20000)
	PRERAM_CBMEM_CONSOLE(CONFIG_MSM89X7_BOOT_IMAGE_ADDR + 0x2254000, 0x20000)
	RAMSTAGE(CONFIG_MSM89X7_BOOT_IMAGE_ADDR + 0x2274000, 0x1000000)
	/* Santoni's QSEECOM override starts at 0x84a00000 (not the
	 * generic MSM8937 0x85b00000). Keep the full declared cache below
	 * that boundary and leave [84400000,84500000) for the diagnostic. */
	POSTRAM_CBFS_CACHE(CONFIG_MSM89X7_BOOT_IMAGE_ADDR + 0x3300000, 0x1000000)
	ASSERT(_eramstage <= _postram_cbfs_cache, "ramstage overlaps postram cache")
#if CONFIG(BOARD_QCOM_MSM89X7_SANTONI)
	ASSERT(_epostram_cbfs_cache <= 0x84a00000, "cache overlaps santoni firmware")
	ASSERT(_estack <= 0x84a00000, "stack overlaps santoni firmware")
	ASSERT(_ettb <= 0x84a00000, "TTB overlaps santoni firmware")
#endif
}
