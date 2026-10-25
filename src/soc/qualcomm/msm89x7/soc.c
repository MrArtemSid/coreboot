/* SPDX-License-Identifier: GPL-2.0-only */

#include <bootmem.h>
#include <commonlib/device_tree.h>
#include <commonlib/helpers.h>
#include <console/console.h>
#include <device/device.h>
#include <device/mmio.h>
#include <soc/addressmap.h>
#include <soc/symbols.h>

/*
 * The stock LK bootloader hands over a device tree in x0 and the
 * bootblock entry code stores its physical address in _lk_dtb. Its
 * /memory and /reserved-memory nodes reflect the memory map that SBL
 * and TrustZone carved out, so they are the most reliable source of
 * usable RAM and reserved regions.
 */
static void lk_dtb_add_ranges(enum bootmem_type tag)
{
	const void *blob;
	struct device_tree_region regions[16];
	u32 offset, addr_cells, size_cells;
	size_t i, count, sub_count;
	u32 sub_nodes[16];

	blob = (const void *)read64((void *)_lk_dtb);
	if (!blob || !fdt_is_valid(blob)) {
		printk(BIOS_INFO, "No valid device tree from primary "
		       "bootloader\n");
		return;
	}

	printk(BIOS_INFO, "Using memory map from primary bootloader DTB\n");

	offset = fdt_find_node_by_path(blob, "/memory", &addr_cells,
				       &size_cells);
	if (offset) {
		count = fdt_read_reg_prop(blob, offset, addr_cells, size_cells,
					  regions, ARRAY_SIZE(regions));
		for (i = 0; i < count; ++i)
			bootmem_add_range(regions[i].addr, regions[i].size,
					  tag);
	}

	offset = fdt_find_node_by_path(blob, "/reserved-memory", &addr_cells,
				       &size_cells);
	if (offset) {
		count = fdt_find_subnodes_by_prefix(blob, offset, "",
						    &addr_cells, &size_cells,
						    sub_nodes,
						    ARRAY_SIZE(sub_nodes));
		for (i = 0; i < count; ++i) {
			sub_count = fdt_read_reg_prop(blob, sub_nodes[i],
						      addr_cells, size_cells,
						      regions,
						      ARRAY_SIZE(regions));
			for (size_t j = 0; j < sub_count; ++j)
				bootmem_add_range(regions[j].addr,
						  regions[j].size,
						  BM_MEM_RESERVED);
		}
	}
}

void bootmem_platform_add_ranges(void)
{
	/* The loaded coreboot image (bootblock, FMAP and CBFS) is still
	 * in use while the payload runs. */
	bootmem_add_range((uintptr_t)_bootblock,
			  (uintptr_t)CONFIG_ROM_SIZE, BM_MEM_RESERVED);

	/* Shared memory used by RPM, modem and TrustZone. */
	bootmem_add_range(SMEM_BASE, SMEM_SIZE, BM_MEM_RESERVED);

	/* Memory map from the primary bootloader takes precedence over
	 * the fallback in soc_read_resources. */
	lk_dtb_add_ranges(BM_MEM_RAM);
}

static void soc_read_resources(struct device *dev)
{
	/* Fallback until the DTB from the primary bootloader has been
	 * parsed; ranges overlap with it are overridden above. */
	ram_range(dev, 0, (uintptr_t)_dram,
		  (uint64_t)CONFIG_MSM89X7_DRAM_SIZE_MB * MiB);
}

static void soc_init(struct device *dev)
{
}

static struct device_operations soc_ops = {
	.read_resources = soc_read_resources,
	.init = soc_init,
};

static void enable_soc_dev(struct device *dev)
{
	dev->ops = &soc_ops;
}

struct chip_operations soc_qualcomm_msm89x7_ops = {
	.name = "SOC Qualcomm MSM89x7",
	.enable_dev = enable_soc_dev,
};
