/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Hand the LK continuous splash over to the payload as a simplefb.
 *
 * LK leaves the panel scanning out a 24bpp RGB888 framebuffer and
 * describes it in its device tree with a disabled simple-framebuffer
 * node. Mainline U-Boot and Linux simplefb drivers only accept 16 and
 * 32bpp formats, so do what lk2nd does for "lk2nd.pass-simplefb=xrgb8888":
 * repack the pixels to XRGB8888 in place, switch the MDP5 source pipe
 * to the new format and stride, and describe the result in the device
 * tree handed to the payload.
 */

#include <arch/mmu.h>
#include <commonlib/device_tree.h>
#include <commonlib/helpers.h>
#include <console/console.h>
#include <device/mmio.h>
#include <soc/addressmap.h>
#include <soc/payload_fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* MDP5 SSPP registers, relative to the pipe base */
#define SSPP_SRC_SIZE		0x00
#define SSPP_SRC0_ADDR		0x14
#define SSPP_SRC_YSTRIDE	0x24
#define SSPP_SRC_FORMAT		0x30
#define SSPP_SRC_UNPACK_PATTERN	0x34

#define MDP_CTL0_FLUSH		(MDSS_MDP_BASE + 0x2000 + 0x18)

/* Values from lk2nd lk2nd/display/cont-splash/pipe.c */
#define SRC_FORMAT_XRGB8888	(3 << 9 | 3 << 6 | 3 << 4 | 3 << 2 | 3 << 0 | \
				 1 << 17 | 3 << 12)
#define UNPACK_PATTERN_XRGB	(3 << 24 | 2 << 16 | 0 << 8 | 1 << 0)

#define DDR_SPLASH_START	0x80000000ULL
#define DDR_SPLASH_END		0xe0000000ULL

/* Source pipes present on msm89x7 and their CTL flush bits. */
static const struct {
	uint32_t offset;
	uint32_t flush;
} pipes[] = {
	{ 0x15000, 1 << 3 },	/* RGB0, used by LK for the splash */
	{ 0x17000, 1 << 4 },	/* RGB1 */
	{ 0x25000, 1 << 11 },	/* DMA0 */
	{ 0x05000, 1 << 0 },	/* VIG0 */
};

struct splash {
	uintptr_t pipe;
	uint32_t flush;
	uint64_t base;
	uint32_t width, height, stride, bpp;
};

static bool find_splash(struct splash *s)
{
	for (size_t i = 0; i < ARRAY_SIZE(pipes); i++) {
		uintptr_t pipe = MDSS_MDP_BASE + pipes[i].offset;
		uint32_t addr = read32((void *)(pipe + SSPP_SRC0_ADDR));
		uint32_t size, bpp, stride;

		if (addr < DDR_SPLASH_START || addr >= DDR_SPLASH_END)
			continue;

		size = read32((void *)(pipe + SSPP_SRC_SIZE));
		bpp = ((read32((void *)(pipe + SSPP_SRC_FORMAT)) >> 9) & 0x3) + 1;
		stride = read32((void *)(pipe + SSPP_SRC_YSTRIDE));

		s->pipe = pipe;
		s->flush = pipes[i].flush;
		s->base = addr;
		s->width = size & 0xffff;
		s->height = size >> 16;
		s->stride = stride;
		s->bpp = bpp;

		if (s->width < 100 || s->width > 4096 || s->height < 100 ||
		    s->height > 8192 || (bpp != 3 && bpp != 4) ||
		    stride < s->width * bpp)
			continue;
		return true;
	}
	return false;
}

static bool reg_base_is(struct device_tree_node *node, u32 ac, u32 sc, uint64_t base)
{
	struct device_tree_region reg;

	return dt_read_reg_prop(node, ac, sc, &reg, 1) == 1 && reg.addr == base;
}

/* Repack RGB888 to XRGB8888 in place, last pixel first. */
static void repack_xrgb8888(const struct splash *s, uint32_t new_stride)
{
	uint8_t *fb = (uint8_t *)(uintptr_t)s->base;

	for (uint32_t y = s->height; y-- > 0;) {
		const uint8_t *src = fb + (size_t)y * s->stride;
		uint32_t *dst = (uint32_t *)(fb + (size_t)y * new_stride);

		for (uint32_t x = s->width; x-- > 0;) {
			const uint8_t *p = src + 3 * x;

			dst[x] = p[0] | p[1] << 8 | p[2] << 16;
		}
	}
}

void *msm89x7_payload_fb_handoff(void *fdt)
{
	static char name[32];
	struct device_tree_node *chosen, *fb, *resmem, *rsv = NULL, *child;
	uint32_t new_stride, phandle;
	uint64_t new_size;
	struct device_tree *tree;
	struct splash s;
	u32 ac, sc;
	void *out;

	if (!find_splash(&s)) {
		printk(BIOS_INFO, "MSM89X7: no splash scanout, simplefb not passed\n");
		return fdt;
	}

	tree = fdt_unflatten(fdt);
	if (!tree)
		return fdt;

	chosen = dt_find_node_by_path(tree, "/chosen", NULL, NULL, 0);
	resmem = dt_find_node_by_path(tree, "/reserved-memory", NULL, NULL, 0);
	fb = chosen ? dt_find_compat(chosen, "simple-framebuffer") : NULL;
	if (!fb || !resmem) {
		printk(BIOS_WARNING, "MSM89X7: no framebuffer/reserved-memory node\n");
		return fdt;
	}

	ac = 2;
	sc = 1;
	dt_read_cell_props(chosen, &ac, &sc);
	if (!reg_base_is(fb, ac, sc, s.base)) {
		printk(BIOS_WARNING, "MSM89X7: DT framebuffer is not the scanout\n");
		return fdt;
	}

	new_stride = s.width * 4;
	new_size = ALIGN_UP((uint64_t)new_stride * s.height, 4 * KiB);
	dt_add_reg_prop(fb, &s.base, &new_size, 1, ac, sc);
	dt_add_u32_prop(fb, "stride", new_stride);
	dt_add_string_prop(fb, "format", "x8r8g8b8");
	dt_add_string_prop(fb, "status", "okay");

	/* Grow the matching no-map reservation and name it like lk2nd
	 * does, so the payload keeps it mapped for the live scanout. */
	ac = 2;
	sc = 1;
	dt_read_cell_props(resmem, &ac, &sc);
	list_for_each(child, resmem->children, list_node) {
		if (reg_base_is(child, ac, sc, s.base)) {
			rsv = child;
			break;
		}
	}
	snprintf(name, sizeof(name), "cont-splash@%llx", (unsigned long long)s.base);
	if (!rsv)
		rsv = dt_add_reserved_memory_region(tree, name, NULL,
						    s.base, new_size, true);
	if (!rsv)
		return fdt;
	rsv->name = name;
	dt_add_reg_prop(rsv, &s.base, &new_size, 1, ac, sc);
	phandle = rsv->phandle;
	if (!phandle) {
		phandle = ++tree->max_phandle;
		rsv->phandle = phandle;
		dt_add_u32_prop(rsv, "phandle", phandle);
	}
	dt_add_u32_prop(fb, "memory-region", phandle);

	out = malloc(dt_flat_size(tree));
	if (!out)
		return fdt;
	dt_flatten(tree, out);

	/* Last log line: the display console keeps drawing RGB888. */
	printk(BIOS_INFO, "MSM89X7: simplefb %ux%u XRGB8888 at 0x%llx for payload\n",
	       s.width, s.height, s.base);

	if (s.bpp == 3) {
		uint64_t start = ALIGN_DOWN(s.base, 4 * KiB);

		mmu_config_range((void *)(uintptr_t)start, s.base + new_size - start,
				 MA_MEM | MA_MEM_NC | MA_RW);
		repack_xrgb8888(&s, new_stride);
		write32((void *)(s.pipe + SSPP_SRC_FORMAT), SRC_FORMAT_XRGB8888);
		write32((void *)(s.pipe + SSPP_SRC_UNPACK_PATTERN), UNPACK_PATTERN_XRGB);
		write32((void *)(s.pipe + SSPP_SRC_YSTRIDE), new_stride);
		write32((void *)MDP_CTL0_FLUSH, s.flush);
	}

	return out;
}
