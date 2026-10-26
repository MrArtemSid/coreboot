/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot_device.h>

/*
 * The whole coreboot ROM (bootblock, FMAP and CBFS) has been loaded
 * into DRAM by the stock Android bootloader and is entered in place,
 * so the CBFS is simply memory-mapped.
 */
static const struct mem_region_device boot_dev =
	MEM_REGION_DEV_RO_INIT((void *)(uintptr_t)CONFIG_MSM89X7_BOOT_IMAGE_ADDR,
			       CONFIG_ROM_SIZE);

const struct region_device *boot_device_ro(void)
{
	return &boot_dev.rdev;
}
