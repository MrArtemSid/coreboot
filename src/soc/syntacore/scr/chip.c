/* SPDX-License-Identifier: GPL-2.0-only */

#include <cbmem.h>
#include <device/device.h>
#include <soc/addressmap.h>

static void scr_enable(struct device *dev)
{
	ram_from_to(dev, 0, SCR_DDR_BASE, cbmem_top());
}

struct chip_operations soc_syntacore_scr_ops = {
	.name = "Syntacore SCR",
	.enable_dev = scr_enable,
};
