/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/bsd/helpers.h>
#include <console/console.h>
#include <delay.h>
#include <device/mmio.h>
#include <soc/addressmap.h>
#include <soc/clock.h>

/*
 * Minimal GCC support. The primary bootloader already brought up GPLL0
 * and every clock needed by coreboot; this only covers the debug UART
 * so that it can be used without relying on LK having left it on.
 * Register offsets from the Linux gcc-msm8917 driver.
 */
#define GCC_REG(off)			((void *)(uintptr_t)(GCC_BASE + (off)))

#define GCC_BLSP1_AHB_CBCR		GCC_REG(0x01008)
#define GCC_BLSP1_UART2_APPS_CBCR	GCC_REG(0x0302c)
#define GCC_BLSP1_UART2_APPS_RCGR	GCC_REG(0x03034)
#define GCC_APCS_GPLL_ENA_VOTE		GCC_REG(0x45000)
#define GCC_APCS_CLOCK_BRANCH_ENA_VOTE	GCC_REG(0x45004)

#define GPLL0_VOTE_BIT			BIT(0)
#define BLSP1_AHB_VOTE_BIT		BIT(10)

#define CBCR_ENABLE			BIT(0)
#define CBCR_OFF			BIT(31)

/* RCG register block: CMD, CFG, M, N, D */
#define RCG_CMD(rcg)			((rcg) + 0x0)
#define RCG_CFG(rcg)			((rcg) + 0x4)
#define RCG_M(rcg)			((rcg) + 0x8)
#define RCG_N(rcg)			((rcg) + 0xc)
#define RCG_D(rcg)			((rcg) + 0x10)
#define RCG_CMD_UPDATE			BIT(0)
#define RCG_CFG_SRC_DIV(div)		((2 * (div) - 1) << 0)
#define RCG_CFG_SRC_SEL(src)		((src) << 8)
#define RCG_CFG_MODE_DUAL_EDGE		(2 << 12)

#define SRC_GPLL0			1

#define POLL_US				200

static enum cb_err wait_clear(void *reg, u32 mask)
{
	for (int i = 0; i < POLL_US; i++) {
		if (!(read32(reg) & mask))
			return CB_SUCCESS;
		udelay(1);
	}
	return CB_ERR;
}

static enum cb_err branch_enable(void *cbcr)
{
	setbits32(cbcr, CBCR_ENABLE);
	return wait_clear(cbcr, CBCR_OFF);
}

/* 7.3728 MHz = GPLL0 (800 MHz) * 144 / 15625 */
static enum cb_err uart_rcg_configure(void *rcg)
{
	const u32 m = 144, n = 15625;

	write32(RCG_M(rcg), m);
	write32(RCG_N(rcg), ~(n - m));
	write32(RCG_D(rcg), ~n);
	write32(RCG_CFG(rcg), RCG_CFG_SRC_SEL(SRC_GPLL0) | RCG_CFG_SRC_DIV(1) |
		RCG_CFG_MODE_DUAL_EDGE);
	setbits32(RCG_CMD(rcg), RCG_CMD_UPDATE);
	return wait_clear(RCG_CMD(rcg), RCG_CMD_UPDATE);
}

enum cb_err msm89x7_clock_enable_uart(void)
{
	setbits32(GCC_APCS_GPLL_ENA_VOTE, GPLL0_VOTE_BIT);
	setbits32(GCC_APCS_CLOCK_BRANCH_ENA_VOTE, BLSP1_AHB_VOTE_BIT);
	if (wait_clear(GCC_BLSP1_AHB_CBCR, CBCR_OFF) != CB_SUCCESS)
		return CB_ERR;

	if (uart_rcg_configure(GCC_BLSP1_UART2_APPS_RCGR) != CB_SUCCESS)
		return CB_ERR;

	return branch_enable(GCC_BLSP1_UART2_APPS_CBCR);
}
