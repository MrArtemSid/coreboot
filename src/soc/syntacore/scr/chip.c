/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/device.h>
#include <soc/soc.h>

struct chip_operations soc_syntacore_scr_ops = {
	.name = "Syntacore SCR",
	.enable_dev = syntacore_soc_enable,
};
