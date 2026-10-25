/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Display console: writes the coreboot log into the framebuffer that
 * the stock Android bootloader (LK) leaves behind as the "continuous
 * splash". The MDP keeps scanning out that buffer on its own, so
 * drawing text there shows up on the panel without any display
 * hardware initialization in coreboot.
 *
 * The framebuffer address, width and height are set in Kconfig. LK
 * uses a 32bpp (XRGB8888) format on this SoC family; the text is
 * drawn white on black cells, which reads correctly regardless of
 * channel order.
 */

#include <commonlib/helpers.h>
#include <console/display_console.h>
#include <device/mmio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

extern const uint8_t font_8x16[256][16];

#define FONT_WIDTH	8
#define FONT_HEIGHT	16

#define FG_COLOR	0xffffffff
#define BG_COLOR	0x00000000

static uint32_t col, row;
static bool enabled;

static void *pixel_base(uint32_t x, uint32_t y)
{
	void *fb = (void *)(uintptr_t)CONFIG_MSM89X7_FB_ADDR;

	return fb + y * CONFIG_MSM89X7_FB_WIDTH * 4 + x * 4;
}

static void draw_char(char c)
{
	const uint8_t *glyph = font_8x16[(uint8_t)c];

	for (uint32_t gy = 0; gy < FONT_HEIGHT; ++gy) {
		uint8_t bits = glyph[gy];
		for (uint32_t gx = 0; gx < FONT_WIDTH; ++gx)
			write32(pixel_base(col * FONT_WIDTH + gx,
					   row * FONT_HEIGHT + gy),
				(bits & (1 << (7 - gx))) ? FG_COLOR : BG_COLOR);
	}
}

static void scroll(void)
{
	uint32_t stride = CONFIG_MSM89X7_FB_WIDTH * 4;
	void *fb = (void *)(uintptr_t)CONFIG_MSM89X7_FB_ADDR;
	uint32_t top = FONT_HEIGHT * stride;

	memmove(fb, fb + top,
		(CONFIG_MSM89X7_FB_HEIGHT - FONT_HEIGHT) * stride);
	memset(fb + (CONFIG_MSM89X7_FB_HEIGHT - FONT_HEIGHT) * stride, 0,
	       FONT_HEIGHT * stride);
}

static void newline(void)
{
	col = 0;
	if (++row >= CONFIG_MSM89X7_FB_HEIGHT / FONT_HEIGHT) {
		row = CONFIG_MSM89X7_FB_HEIGHT / FONT_HEIGHT - 1;
		scroll();
	}
}

static void display_consoleputc(unsigned char byte)
{
	switch (byte) {
	case '\n':
		newline();
		return;
	case '\r':
		col = 0;
		return;
	case '\t':
		col = MIN(ALIGN_UP(col + 1, 8),
			  CONFIG_MSM89X7_FB_WIDTH / FONT_WIDTH - 1);
		return;
	default:
		break;
	}

	draw_char(byte);
	if (++col >= CONFIG_MSM89X7_FB_WIDTH / FONT_WIDTH)
		newline();
}

void __display_console_init(void)
{
	/* The splash framebuffer content is left as-is; the cursor just
	 * starts at the top left corner on top of it. */
	enabled = true;
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
