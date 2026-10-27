/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/cache.h>
#include <arch/mmu.h>
#include <cbmem.h>
#if CONFIG(MSM89X7_PARSE_DT_CARVEOUTS)
#include <commonlib/device_tree.h>
#endif
#include <commonlib/helpers.h>
#include <console/console.h>
#include <device/mmio.h>
#include <soc/addressmap.h>
#include <soc/display_console.h>
#include <soc/mmu.h>
#include <soc/symbols.h>
#include <symbols.h>

#if CONFIG(MSM89X7_PARSE_DT_CARVEOUTS)
#define MAX_CARVEOUTS 16

struct carveout {
	uint64_t start;
	uint64_t end;
};

/* Use the existing FDT reader instead of a second structure walker. */
static size_t read_carveouts(struct carveout *out, size_t max)
{
	const void *blob = (const void *)(uintptr_t)read64((void *)_lk_dtb);
	struct device_tree_region regions[MAX_CARVEOUTS];
	struct fdt_property prop;
	u32 nodes[MAX_CARVEOUTS], ac, sc;
	size_t count = 0;

	if (!blob || !fdt_is_valid(blob))
		die("No valid handoff DTB\n");

	static const char reserved_memory[] __aligned(8) = "/reserved-memory";
	u32 parent = fdt_find_node_by_path(blob, reserved_memory, &ac, &sc);

	if (!parent)
		die("No reserved-memory node\n");

	size_t n = fdt_find_subnodes_by_prefix(blob, parent, "", &ac, &sc,
					      nodes, ARRAY_SIZE(nodes));
	if (n == ARRAY_SIZE(nodes))
		die("Too many reserved-memory nodes\n");

	for (size_t i = 0; i < n; i++) {
		/* Like U-Boot's mach-snapdragon: only no-map nodes without a
		 * compatible are holes. Nodes with a compatible (e.g. qcom,smem)
		 * stay mapped as normal memory for their drivers. */
		if (!fdt_read_prop(blob, nodes[i], "no-map", &prop) ||
		    fdt_read_prop(blob, nodes[i], "compatible", &prop))
			continue;

		size_t nr = fdt_read_reg_prop(blob, nodes[i], ac, sc,
					      regions, ARRAY_SIZE(regions));
		if (nr == ARRAY_SIZE(regions))
			die("Too many reserved-memory ranges\n");

		for (size_t j = 0; j < nr; j++) {
			if (!regions[j].size)
				continue;
			if (count == max)
				die("Too many carveouts\n");
			out[count].start = ALIGN_DOWN(regions[j].addr, 4 * KiB);
			out[count].end = ALIGN_UP(regions[j].addr + regions[j].size,
						 4 * KiB);
			count++;
		}
	}

	for (size_t i = 1; i < count; i++) {
		struct carveout c = out[i];
		size_t j;

		for (j = i; j && out[j - 1].start > c.start; j--)
			out[j] = out[j - 1];
		out[j] = c;
	}

	return count;
}

static void map_dram_with_carveouts(void)
{
	struct carveout holes[MAX_CARVEOUTS];
	uint64_t pos = (uintptr_t)_dram;
	uint64_t end = pos + (uint64_t)CONFIG_MSM89X7_DRAM_SIZE_MB * MiB;
	size_t i, n = read_carveouts(holes, ARRAY_SIZE(holes));

	for (i = 0; i < n; i++) {
		uint64_t s = MAX(holes[i].start, pos);
		uint64_t e = MIN(holes[i].end, end);

		if (e <= s)
			continue;
		if (s > pos)
			mmu_config_range((void *)(uintptr_t)pos, s - pos,
					 MA_MEM | MA_RW);
		printk(BIOS_INFO, "MSM89X7: leaving [0x%llx:0x%llx) unmapped\n",
		       (unsigned long long)s, (unsigned long long)e);
		pos = e;
	}
	if (end > pos)
		mmu_config_range((void *)(uintptr_t)pos, end - pos,
				 MA_MEM | MA_RW);
}
#endif

void msm89x7_mmu_init(void)
{
	mmu_init();

	/* 1. System peripherals below DRAM (0 .. 1 GiB). */
	mmu_config_range((void *)0, (uintptr_t)_dram, MA_DEV | MA_RW);

#if CONFIG(MSM89X7_PARSE_DT_CARVEOUTS)
	map_dram_with_carveouts();
#else
	uintptr_t dt = read64((void *)_lk_dtb);
	uintptr_t cbmem_top = cbmem_top_chipset();

	/* 2. coreboot execution region in DRAM (ROM, stack, TTB, stages, cache, payload).
	 * Fits in 74 MiB, well below the first firmware carveout at 0x84a00000. */
	mmu_config_range((void *)0x80000000, 0x04a00000, MA_MEM | MA_RW);

	/* 3. CBMEM allocation window at the top of physical DRAM. */
	mmu_config_range((void *)(cbmem_top - 2 * MiB), 2 * MiB, MA_MEM | MA_RW);

	/* 4. Handoff device tree blob from LK. */
	mmu_config_range((void *)ALIGN_DOWN(dt, 2 * MiB), 2 * MiB,
			 MA_MEM | MA_MEM_NC | MA_RW);
#endif

	/* Continuous splash framebuffer scanned out by MDP5. */
	if (CONFIG(MSM89X7_DISPLAY_CONSOLE)) {
		uint64_t base;
		size_t size;

		display_console_fb_region(&base, &size);
		if (size) {
			uint64_t end = ALIGN_UP(base + size, 4 * KiB);

			base = ALIGN_DOWN(base, 4 * KiB);
			mmu_config_range((void *)(uintptr_t)base, end - base,
					 MA_MEM | MA_MEM_NC | MA_RW);
		}
	}

	/* lk2nd ramoops console buffer. */
	mmu_config_range((void *)0xbff80000, 0x80000, MA_MEM | MA_MEM_NC | MA_RW);

	dcache_clean_invalidate_all();
	mmu_enable();
	printk(BIOS_INFO, "MSM89X7: MMU enabled (%s)\n",
	       CONFIG(MSM89X7_PARSE_DT_CARVEOUTS) ? "carveouts" : "whitelist");
}
