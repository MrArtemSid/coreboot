/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/device_tree.h>
#include <commonlib/helpers.h>
#include <device/mmio.h>
#include <endian.h>
#include <symbols.h>
#include <arch/mmu.h>
#include <arch/cache.h>
#include <arch/hlt.h>
#include <arch/lib_helpers.h>
#include <soc/mmu.h>
#include <soc/symbols.h>
#include <soc/addressmap.h>
#include <soc/ramoops.h>
#include <soc/display_console.h>
#include <console/console.h>
#include <console/ramoops_console.h>

#define MAX_CARVEOUTS 16

struct carveout {
	uint64_t start;
	uint64_t end;
};

/*
 * Firmware regions marked no-map in the DT handed over by the primary
 * bootloader are protected by XPUs. With a Normal cacheable alias the
 * prefetcher may touch them, which TrustZone answers with a delayed
 * reset rather than an abort. Leave them out of the page tables
 * entirely, like U-Boot does on Qualcomm SoCs. Regions with a
 * compatible (SMEM, RMTFS) are accessed by drivers and stay mapped.
 *
 * This runs in the bootblock with the MMU off, so all memory is Device
 * and any unaligned access faults. The generic FDT helpers copy strings
 * with an 8-byte memcpy, so walk the structure block here with aligned
 * 32-bit loads and byte-wise string compares only.
 */

/* All callers pass 4-byte aligned FDT offsets. */
static uint32_t fdt32(const void *p)
{
	return be32toh(read32(p));
}

static bool name_eq(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

static bool node_name_eq(const char *node, const char *name)
{
	/* Match "name" and "name@unit". */
	while (*name && *node == *name) {
		node++;
		name++;
	}
	return !*name && (!*node || *node == '@');
}

static uint64_t read_cells(const uint8_t *p, u32 cells)
{
	return cells == 2 ? (uint64_t)fdt32(p) << 32 | fdt32(p + 4)
			  : fdt32(p);
}

static size_t read_carveouts(struct carveout *out, size_t max)
{
	const uint8_t *blob = (const uint8_t *)(uintptr_t)read64((void *)_lk_dtb);
	const uint8_t *p, *end, *strings;
	u32 root_ac = 2, root_sc = 1, ac = 0, sc = 0;
	int depth = 0, in_rmem = 0;
	bool no_map = false, compat = false;
	const uint8_t *reg = NULL;
	u32 reg_len = 0;
	size_t i, j, count = 0;

	if (!blob || ((uintptr_t)blob & 3) || fdt32(blob) != FDT_HEADER_MAGIC)
		return 0;

	p = blob + fdt32(blob + 8);
	end = blob + fdt32(blob + 4);
	strings = blob + fdt32(blob + 12);

	while (p < end) {
		u32 token = fdt32(p);

		p += 4;
		if (token == FDT_TOKEN_BEGIN_NODE) {
			const char *name = (const char *)p;

			depth++;
			if (depth == 2 && node_name_eq(name, "reserved-memory")) {
				in_rmem = 1;
				ac = root_ac;
				sc = root_sc;
			}
			if (in_rmem && depth == 3) {
				no_map = compat = false;
				reg = NULL;
			}
			while (*p)
				p++;
			p = (const uint8_t *)ALIGN_UP((uintptr_t)p + 1, 4);
		} else if (token == FDT_TOKEN_PROPERTY) {
			u32 len = fdt32(p);
			const char *pname = (const char *)strings + fdt32(p + 4);
			const uint8_t *data = p + 8;

			if (depth == 1 && name_eq(pname, "#address-cells"))
				root_ac = fdt32(data);
			else if (depth == 1 && name_eq(pname, "#size-cells"))
				root_sc = fdt32(data);
			else if (in_rmem && depth == 2 &&
				 name_eq(pname, "#address-cells"))
				ac = fdt32(data);
			else if (in_rmem && depth == 2 &&
				 name_eq(pname, "#size-cells"))
				sc = fdt32(data);
			else if (in_rmem && depth == 3) {
				if (name_eq(pname, "no-map"))
					no_map = true;
				else if (name_eq(pname, "compatible"))
					compat = true;
				else if (name_eq(pname, "reg")) {
					reg = data;
					reg_len = len;
				}
			}
			p = data + ALIGN_UP(len, 4);
		} else if (token == FDT_TOKEN_END_NODE) {
			if (in_rmem && depth == 3 && no_map && !compat && reg &&
			    ac && ac <= 2 && sc && sc <= 2) {
				u32 entry = 4 * (ac + sc);

				for (j = 0; j + entry <= reg_len && count < max;
				     j += entry) {
					uint64_t base = read_cells(reg + j, ac);
					uint64_t size = read_cells(reg + j + 4 * ac,
								   sc);
					if (!size)
						continue;
					out[count].start = ALIGN_DOWN(base, 4 * KiB);
					out[count].end = ALIGN_UP(base + size,
								   4 * KiB);
					count++;
				}
			}
			if (in_rmem && depth == 2)
				in_rmem = 0;
			depth--;
		} else if (token == FDT_TOKEN_NOP) {
			continue;
		} else {
			break;
		}
	}

	/* Insertion sort by start address. */
	for (i = 1; i < count; i++) {
		struct carveout c = out[i];
		for (j = i; j > 0 && out[j - 1].start > c.start; j--)
			out[j] = out[j - 1];
		out[j] = c;
	}
	return count;
}

static void map_dram(void)
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

void msm89x7_mmu_init(void)
{
	mmu_init();
	mmu_config_range((void *)0, (uintptr_t)_dram, MA_DEV | MA_RW);
	map_dram();
	mmu_config_range((void *)SMEM_BASE, SMEM_SIZE, MA_DEV | MA_RW);
	if (CONFIG(MSM89X7_DISPLAY_CONSOLE)) {
		uint64_t base;
		size_t size;
		display_console_fb_region(&base, &size);
		if (!size) {
			base = CONFIG_MSM89X7_FB_ADDR;
			size = (size_t)CONFIG_MSM89X7_FB_WIDTH *
			       CONFIG_MSM89X7_FB_HEIGHT * 4;
		}
		mmu_config_range((void *)(uintptr_t)base, size,
				 MA_MEM | MA_MEM_NC | MA_RW);
	}
	mmu_config_range((void *)(uintptr_t)0xbff80000, 0x80000,
			 MA_MEM | MA_MEM_NC | MA_RW);
	/* Early-fault beacons: raw ramoops bytes survive even without a
	 * working console and mark exactly which enable step died. */
	__ramoops_tx_byte('1');
	dcache_clean_invalidate_all();
	__ramoops_tx_byte('2');
	mmu_enable();
	__ramoops_tx_byte('3');
	printk(BIOS_INFO, "MSM89X7: MMU enabled\n");
}
