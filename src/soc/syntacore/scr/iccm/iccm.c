/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/device_tree.h>
#include <console/console.h>
#include <endian.h>
#include <mcall.h>
#include <soc/ipi.h>

void scr_ipi_send(unsigned int hart)
{
	const void *fdt = HLS()->fdt;
	struct fdt_property prop;
	u32 cpus[CONFIG_MAX_CPUS];
	u32 parent = fdt_find_node_by_path(fdt, "/cpus", NULL, NULL);
	size_t count = fdt_find_subnodes_by_prefix(fdt, parent, "cpu@", NULL, NULL,
						cpus, ARRAY_SIZE(cpus));
	u32 phandle = 0;

	for (size_t i = 0; i < count; i++) {
		if (!fdt_read_prop(fdt, cpus[i], "reg", &prop) ||
		    fdt_read_int_prop(&prop, 1) != hart)
			continue;
		if (fdt_read_prop(fdt, cpus[i], "phandle", &prop))
			phandle = fdt_read_int_prop(&prop, 1);
		break;
	}

	parent = fdt_find_node_by_path(fdt, "/soc/cluster_bus/iccm", NULL, NULL);
	if (!phandle || !parent || !fdt_read_prop(fdt, parent, "mbox-mapping", &prop) ||
	    prop.size % (2 * sizeof(u32)))
		die("Invalid ICCM mailbox mapping");

	const u32 *map = prop.data;

	for (size_t i = 0; i < prop.size / sizeof(u32); i += 2) {
		if (be32_to_cpu(map[i]) != phandle)
			continue;
		if (!scr_iccm_send(be32_to_cpu(map[i + 1])))
			die("ICCM send failed");
		return;
	}
	die("No ICCM mailbox for hart");
}

void scr_ipi_clear(void)
{
	if (!scr_iccm_clear())
		die("ICCM drain failed");
}
