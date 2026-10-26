/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/device_tree.h>
#include <console/console.h>
#include <device/device.h>
#include <halt.h>
#include <mcall.h>
#include <soc/scr_hal.h>
#include <string.h>

#define SCR_MAX_MEMORY_REGIONS 16

static void scr_memory_resources(struct device *dev)
{
	const void *fdt = HLS()->fdt;
	struct device_tree_region regions[SCR_MAX_MEMORY_REGIONS] = {0};
	u32 nodes[SCR_MAX_MEMORY_REGIONS];
	u32 addr_cells, size_cells;
	unsigned long index = 0;
	size_t count;

	if (!fdt || !fdt_is_valid(fdt))
		die("SCR: missing or invalid FDT\n");

	/* CBMEM top is a staging limit, not the end of installed RAM. */
	count = fdt_read_memory_regions(fdt, regions, ARRAY_SIZE(regions));
	if (!count || count >= ARRAY_SIZE(regions))
		die("SCR: missing or too many FDT memory regions\n");
	for (size_t i = 0; i < count; i++) {
		if (!regions[i].size || regions[i].addr + regions[i].size < regions[i].addr)
			die("SCR: invalid FDT memory region\n");
		ram_range(dev, index++, regions[i].addr, regions[i].size);
	}

	/* Include carveouts even when the DT excludes them from /memory. */
	u32 reserved = fdt_find_node_by_path(fdt, "/reserved-memory", &addr_cells,
					     &size_cells);
	if (!reserved)
		return;
	count = fdt_find_subnodes_by_prefix(fdt, reserved, "", &addr_cells, &size_cells,
					   nodes, ARRAY_SIZE(nodes));
	if (count >= ARRAY_SIZE(nodes))
		die("SCR: too many FDT reserved-memory nodes\n");
	for (size_t i = 0; i < count; i++) {
		struct fdt_property prop;

		if (fdt_read_prop(fdt, nodes[i], "status", &prop) &&
		    (prop.size != sizeof("okay") || memcmp(prop.data, "okay", sizeof("okay"))) &&
		    (prop.size != sizeof("ok") || memcmp(prop.data, "ok", sizeof("ok"))))
			continue;
		size_t n = fdt_read_reg_prop(fdt, nodes[i], addr_cells, size_cells,
					     regions, ARRAY_SIZE(regions));
		if (n >= ARRAY_SIZE(regions))
			die("SCR: too many FDT reserved-memory regions\n");
		for (size_t j = 0; j < n; j++) {
			if (!regions[j].size || regions[j].addr + regions[j].size < regions[j].addr)
				die("SCR: invalid FDT reserved-memory region\n");
			reserved_ram_range(dev, index++, regions[j].addr, regions[j].size);
		}
	}
}

static void scr_mainboard_init(struct device *dev)
{
	(void)dev;

	/* The stock SiFive PLIC driver does not program SCR source modes. */
	if (CONFIG(SCR9_L2_QEMU_VIRTIO) || CONFIG(SCR9_L3_QEMU_VIRTIO)) {
		scr_hal_plic_set_level_high(2); /* Ethernet DMA */
		scr_hal_plic_set_level_high(3);
	}

	if (CONFIG(SCR9_L2_QEMU_VIRTIO)) {
		scr_hal_plic_set_level_high(1); /* UART0 */
		for (int irq = 12; irq <= 17; irq++)
			scr_hal_plic_set_level_high(irq);
	}

	if (CONFIG(SCR9_L3_QEMU_VIRTIO)) {
		scr_hal_plic_set_level_high(60); /* UART0 */
		for (int irq = 240; irq <= 247; irq++)
			scr_hal_plic_set_level_high(irq);
	}
}

struct device_operations scr_cpu_ops = {
	.read_resources = scr_memory_resources,
};

struct chip_operations mainboard_ops = {
	.name = "Syntacore " CONFIG_MAINBOARD_PART_NUMBER,
	.enable_dev = scr_mainboard_init,
};
