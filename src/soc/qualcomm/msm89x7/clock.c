/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <device/mmio.h>
#include <soc/addressmap.h>
#include <soc/clock.h>
#include <types.h>

static void *gcc_reg(uint32_t offset)
{
	return (void *)((uintptr_t)GCC_BASE + offset);
}

static bool clock_is_off(uint32_t cbcr_offset)
{
	return !!(read32(gcc_reg(cbcr_offset)) & BIT(CBCR_CLK_OFF_BIT));
}

void clock_enable_vote(uint32_t cbcr_offset, uint32_t vote_bit)
{
	/* Set vote bit in GCC_APCS_CLOCK_BRANCH_EN_VOTE */
	setbits32(gcc_reg(GCC_APCS_CLOCK_BRANCH_EN_VOTE), BIT(vote_bit));

	/* Wait until clock turns on (CLK_OFF bit 31 goes low) */
	int timeout = 10000;
	while (clock_is_off(cbcr_offset) && --timeout)
		;
}

void clock_enable_branch(uint32_t cbcr_offset)
{
	/* Set enable bit */
	setbits32(gcc_reg(cbcr_offset), BIT(CBCR_CLK_EN_BIT));

	/* Wait until clock turns on */
	int timeout = 10000;
	while (clock_is_off(cbcr_offset) && --timeout)
		;
}

void clock_init(void)
{
	/* Enable AHB bus clock for BLSP1 and BLSP2 */
	clock_enable_vote(GCC_BLSP1_AHB_CBCR, BLSP1_AHB_CLK_ENA);
	clock_enable_vote(GCC_BLSP2_AHB_CBCR, BLSP2_AHB_CLK_ENA);

	/* Enable MDSS clocks if MDSS/display is active */
	clock_enable_branch(GCC_MDSS_AHB_CBCR);
	clock_enable_branch(GCC_MDSS_AXI_CBCR);

	printk(BIOS_INFO, "MSM89X7: GCC basic clocks initialized\n");
}
