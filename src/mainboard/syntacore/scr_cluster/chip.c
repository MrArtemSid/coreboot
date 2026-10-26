/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/device.h>

#if ENV_RAMSTAGE
extern struct device_operations scr_cpu_ops;

static void scr_enable_cpu_cluster(struct device *dev)
{
	if (dev->path.type == DEVICE_PATH_CPU_CLUSTER)
		dev->ops = &scr_cpu_ops;
}
#endif

struct chip_operations mainboard_syntacore_scr_cluster_ops = {
	.name = "Syntacore " CONFIG_MAINBOARD_PART_NUMBER,
#if ENV_RAMSTAGE
	.enable_dev = scr_enable_cpu_cluster,
#endif
};
