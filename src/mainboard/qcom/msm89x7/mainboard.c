/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/device.h>

static void mainboard_enable(struct device *dev)
{
}

struct chip_operations mainboard_ops = {
	.name = "QCOM MSM89x7 generic",
	.enable_dev = mainboard_enable,
};
