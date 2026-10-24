/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot_device.h>
#include <soc/addressmap.h>

/*
 * The CBFS image lives as a read-only copy in DDR. On FPGA boards this
 * window is the SPI NOR XIP mapping, in SCR QEMU it is the region the
 * coreboot.rom image is preloaded to:
 *
 *   -device loader,file=build/coreboot.rom,addr=0x<SCR_CBFS_WINDOW_BASE>
 */
static const struct mem_region_device boot_dev =
	MEM_REGION_DEV_RO_INIT(SCR_CBFS_WINDOW_BASE, CONFIG_ROM_SIZE);

const struct region_device *boot_device_ro(void)
{
	return &boot_dev.rdev;
}
