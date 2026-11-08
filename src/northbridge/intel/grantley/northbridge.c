/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <device/device.h>
#include "chip.h"
#include "iio.h"

static void grantley_skeleton_stop(struct device *dev)
{
	die("Grantley: ramstage reached; PCI resource initialization unavailable.\n");
}

struct device_operations grantley_pci_domain_ops = {
	.read_resources = noop_read_resources,
	.set_resources = noop_set_resources,
	.init = grantley_skeleton_stop,
};

static void grantley_init(void *chip_info)
{
	grantley_iio_init(chip_info);
}

struct chip_operations northbridge_intel_grantley_ops = {
	.name = "Intel Haswell-EP / Broadwell-EP Northbridge",
	.init = grantley_init,
};
