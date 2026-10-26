/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Display console: writes the coreboot log into the framebuffer that
 * the stock Android bootloader (LK) leaves behind as the "continuous
 * splash". The MDP keeps scanning out that buffer on its own, so
 * drawing text there shows up on the panel without any display
 * hardware initialization in coreboot.
 *
 * Instead of hardcoding the framebuffer geometry, the live
 * configuration of the MDP5 source pipes is read: the pipe that LK
 * programmed for the splash carries the actual framebuffer address,
 * panel width, stride and pixels-per-pixel (the splash is 24bpp RGB
 * on this SoC family, not 32bpp). Text is drawn white on black cells,
 * which reads correctly regardless of the channel order.
 */

#include <arch/lib_helpers.h>
#include <commonlib/helpers.h>
#include <console/console.h>
#include <console/display_console.h>
#include <device/mmio.h>
#include <rules.h>
#include <soc/display_console.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <soc/ramoops.h>
#include <soc/addressmap.h>

extern const uint8_t font_8x16[256][16];

#define FONT_WIDTH	8
#define FONT_HEIGHT	16

/* MDP5 SSPP registers, relative to the pipe base */
#define SSPP_SRC_SIZE		0x00	/* height << 16 | width */
#define SSPP_SRC0_ADDR		0x14
#define SSPP_SRC_YSTRIDE	0x24	/* bytes per line */
#define SSPP_SRC_FORMAT		0x30	/* CPP [10:9] = bytes per pixel - 1 */

/* Source pipes LK may have used for the splash, offsets from the MDP
 * base. Only the pipes that exist on msm89x7 (per the Linux mdp5
 * configuration: VIG_0, RGB_0/1, DMA_0) — reading an unimplemented
 * MDP block hangs the bus. RGB_0 first, it is what LK uses for the
 * splash on these devices. */
static const uint32_t sspp_pipes[] = {
	0x15000, 0x17000, 0x25000, 0x5000,
};

#define DDR_START	0x80000000ULL
#define DDR_END		0xE0000000ULL

static uint64_t fb_addr;
static uint32_t fb_stride, fb_width, fb_height, fb_bpp;
static uint32_t col, row, max_col, max_row;
static bool enabled;

/* Returns the framebuffer region the display console draws to, so the
 * MMU setup can map it non-cacheable. Zero size if nothing detected. */
void display_console_fb_region(uint64_t *base, size_t *size)
{
	*base = fb_addr;
	*size = fb_addr ? fb_stride * fb_height : 0;
}

static void detect_framebuffer(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(sspp_pipes); ++i) {
		uint32_t pipe = MDSS_MDP_BASE + sspp_pipes[i];
		uint32_t size, format, stride, width, height, bpp;
		uint32_t addr = read32((void *)(uintptr_t)(pipe + SSPP_SRC0_ADDR));

		if (addr < DDR_START || addr >= DDR_END)
			continue;

		size = read32((void *)(uintptr_t)(pipe + SSPP_SRC_SIZE));
		width = size & 0xffff;
		height = size >> 16;
		if (width < 100 || width > 4096 || height < 100 ||
		    height > 8192)
			continue;

		format = read32((void *)(uintptr_t)(pipe + SSPP_SRC_FORMAT));
		bpp = ((format >> 9) & 0x3) + 1;
		if (bpp < 3 || bpp > 4)
			continue;

		stride = read32((void *)(uintptr_t)(pipe + SSPP_SRC_YSTRIDE));
		if (stride < width * bpp)
			continue;

		fb_addr = addr;
		fb_stride = stride;
		fb_width = width;
		fb_height = height;
		fb_bpp = bpp;
		max_col = fb_width / FONT_WIDTH;
		max_row = fb_height / FONT_HEIGHT;
		return;
	}

	/* Nothing detected (splash disabled?): fall back to the Kconfig
	 * geometry. Drawing is harmless even if it is invisible. */
	printk(BIOS_ERR, "display console: no MDP pipe detected, using "
	       "Kconfig fallback\n");
	fb_addr = CONFIG_MSM89X7_FB_ADDR;
	fb_stride = CONFIG_MSM89X7_FB_WIDTH * 4;
	fb_width = CONFIG_MSM89X7_FB_WIDTH;
	fb_height = CONFIG_MSM89X7_FB_HEIGHT;
	fb_bpp = 4;
	max_col = fb_width / FONT_WIDTH;
	max_row = fb_height / FONT_HEIGHT;
}

static void *pixel_base(uint32_t x, uint32_t y)
{
	return (void *)(uintptr_t)(fb_addr + y * fb_stride + x * fb_bpp);
}

static void pixel_put(uint32_t x, uint32_t y, int on)
{
	if (fb_bpp == 3) {
		volatile uint8_t *p = pixel_base(x, y);
		uint8_t v = on ? 0xff : 0x00;
		p[0] = v;
		p[1] = v;
		p[2] = v;
	} else {
		write32(pixel_base(x, y), on ? 0xffffffff : 0x00000000);
	}
}

static void draw_char(char c)
{
	const uint8_t *glyph = font_8x16[(uint8_t)c];

	for (uint32_t gy = 0; gy < FONT_HEIGHT; ++gy) {
		uint8_t bits = glyph[gy];
		for (uint32_t gx = 0; gx < FONT_WIDTH; ++gx)
			pixel_put(col * FONT_WIDTH + gx,
				  row * FONT_HEIGHT + gy,
				  bits & (1 << (7 - gx)));
	}
}

static void scroll(void)
{
	void *fb = (void *)(uintptr_t)fb_addr;
	uint32_t line = FONT_HEIGHT * fb_stride;

	memmove(fb, fb + line, (fb_height - FONT_HEIGHT) * fb_stride);
	memset(fb + (fb_height - FONT_HEIGHT) * fb_stride, 0,
	       FONT_HEIGHT * fb_stride);
}

/* The cursor lives in IMEM (not BSS): every stage has its own BSS and
 * would otherwise restart at (0,0), drawing over the previous stage's
 * log. IMEM survives stage transitions and warm resets. */
static void cursor_save(void)
{
	write32((void *)MSM89X7_IMEM_CURSOR_ADDR, row);
	write32((void *)(MSM89X7_IMEM_CURSOR_ADDR + 4), col);
}

static void cursor_restore(void)
{
	uint32_t r = read32((void *)MSM89X7_IMEM_CURSOR_ADDR);
	uint32_t c = read32((void *)(MSM89X7_IMEM_CURSOR_ADDR + 4));

	if (r < max_row && c < max_col) {
		row = r;
		col = c;
	}
}

static void newline(void)
{
	col = 0;
	if (++row >= max_row) {
		row = max_row - 1;
		scroll();
	}
	cursor_save();
}

static void display_consoleputc(unsigned char byte)
{
	static enum { ESC_NONE, ESC_START, ESC_CSI } escape_state;

	/* Pet watchdog on console activity so long dumps don't bite */
	if (CONFIG(MSM89X7_RAMOOPS_CONSOLE))
		msm89x7_wdt_pet();

	/* printk sends ANSI color sequences to all interactive consoles.
	 * This console has no colors; consume CSI sequences instead of
	 * drawing the escape character and its parameters as glyphs. */
	if (byte == '\x1b') {
		escape_state = ESC_START;
		return;
	}
	if (escape_state == ESC_START) {
		escape_state = byte == '[' ? ESC_CSI : ESC_NONE;
		return;
	}
	if (escape_state == ESC_CSI) {
		if (byte >= 0x40 && byte <= 0x7e)
			escape_state = ESC_NONE;
		return;
	}

	switch (byte) {
	case '\n':
		newline();
		return;
	case '\r':
		col = 0;
		return;
	case '\t':
		col = MIN(ALIGN_UP(col + 1, 8), max_col - 1);
		return;
	default:
		break;
	}

	draw_char(byte);
	if (++col >= max_col)
		newline();
	else
		cursor_save();
}

void __display_console_init(void)
{
	/* Each stage has its own BSS: rediscover the inherited scanout so
	 * enabled and the geometry are valid in romstage and ramstage too. */
	detect_framebuffer();

	if (ENV_BOOTBLOCK) {
		/* Clear the splash with plain aligned 32-bit stores:
		 * the bootblock console runs before the MMU is enabled,
		 * where a library memset may fault on wide/unaligned
		 * accesses to device memory. */
		volatile uint32_t *p = (void *)(uintptr_t)fb_addr;
		size_t words = (size_t)fb_stride * fb_height / 4;

		for (size_t i = 0; i < words; ++i)
			p[i] = 0;
		row = 0;
		col = 0;
		cursor_save();
	} else {
		cursor_restore();
	}

	enabled = true;

	printk(BIOS_NOTICE, "display console [%s]: fb=0x%llx %ux%u "
	       "stride=%u bpp=%u row=%u col=%u\n",
	       ENV_BOOTBLOCK ? "bootblock" : ENV_RAMSTAGE ? "ramstage" :
				       "romstage",
	       fb_addr, fb_width, fb_height, fb_stride, fb_bpp, row, col);
}

void __display_console_tx_byte(unsigned char data)
{
	if (!enabled)
		return;

	display_consoleputc(data);
}

void __display_console_tx_flush(void)
{
}
