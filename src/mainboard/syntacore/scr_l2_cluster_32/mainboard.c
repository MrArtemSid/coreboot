/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <device/device.h>
#include <symbols.h>
#include <cbmem.h>
#include <soc/addressmap.h>

/*
 * RAM declaration, SiFive-style: the resource allocator derives the
 * OS-visible RAM map from the resources declared on the mainboard
 * device, so the payload staging area (all of DDR up to cbmem top)
 * has to be reported here.
 */
static void scr_mainboard_init(struct device *dev)
{
	ram_from_to(dev, 0, (uintptr_t)_dram, cbmem_top());
}

struct chip_operations mainboard_ops = {
	.name = "Syntacore SCR9 L3",
	.enable_dev = scr_mainboard_init,
};
