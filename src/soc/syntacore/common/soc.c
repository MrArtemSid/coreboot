/* SPDX-License-Identifier: GPL-2.0-only */

#include <cbmem.h>
#include <device/device.h>
#include <soc/addressmap.h>
#include <soc/soc.h>

/*
 * The CPU cluster owns the platform RAM. Declaring the ranges belongs to the
 * read_resources phase; enabling the device only selects these operations.
 */
static void syntacore_cpu_cluster_read_resources(struct device *dev)
{
	ram_from_to(dev, 0, SCR_DDR_BASE, cbmem_top());
	if (CONFIG(SCR_PRELOADED_PAYLOAD))
		reserved_ram_from_to(dev, 1, SCR_OPENSBI_BASE, SCR_OPENSBI_BASE + SCR_OPENSBI_SIZE);
}

static struct device_operations syntacore_cpu_cluster_ops = {
	.read_resources = syntacore_cpu_cluster_read_resources,
	/* RAM ranges are fixed by the platform memory map; nothing to program. */
	.set_resources = noop_set_resources,
};

void syntacore_soc_enable(struct device *dev)
{
	if (dev->path.type != DEVICE_PATH_CPU_CLUSTER)
		return;

	dev->ops = &syntacore_cpu_cluster_ops;
}
