/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot_device.h>
#include <soc/addressmap.h>

static const struct mem_region_device boot_dev =
	MEM_REGION_DEV_RO_INIT(SCR_CBFS_BASE, CONFIG_ROM_SIZE);

const struct region_device *boot_device_ro(void)
{
	return &boot_dev.rdev;
}
