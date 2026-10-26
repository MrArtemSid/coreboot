/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __SOC_QUALCOMM_MSM89X7_CLOCK_H__
#define __SOC_QUALCOMM_MSM89X7_CLOCK_H__

#include <soc/addressmap.h>
#include <types.h>

/* APCS Clock Branch Vote Bits (register GCC_APCS_CLOCK_BRANCH_EN_VOTE @ 0x45004) */
#define BLSP1_AHB_CLK_ENA		10
#define BLSP2_AHB_CLK_ENA		20
#define BOOT_ROM_AHB_CLK_ENA		7
#define CRYPTO_AHB_CLK_ENA		0
#define CRYPTO_AXI_CLK_ENA		1

/* CBCR bitfields */
#define CBCR_CLK_OFF_BIT		31
#define CBCR_CLK_EN_BIT			0

/* RCG2 bitfields */
#define RCG_CMD_UPDATE_BIT		0
#define RCG_CFG_MODE_DUAL_EDGE		(2 << 12)
#define RCG_CFG_SRC_SEL_GPLL0		(1 << 8)
#define RCG_CFG_SRC_SEL_XO		(0 << 8)

/* Specific CBCR and RCG offsets from GCC_BASE (0x01800000) */
#define GCC_APCS_CLOCK_BRANCH_EN_VOTE	0x45004

#define GCC_BLSP1_AHB_CBCR		0x01008
#define GCC_BLSP2_AHB_CBCR		0x0b008

#define GCC_BLSP1_UART2_APPS_CBCR	0x0302c
#define GCC_BLSP1_UART2_APPS_CMD_RCGR	0x03034
#define GCC_BLSP1_UART2_APPS_CFG_RCGR	0x03038
#define GCC_BLSP1_UART2_APPS_M		0x0303c
#define GCC_BLSP1_UART2_APPS_N		0x03040
#define GCC_BLSP1_UART2_APPS_D		0x03044

#define GCC_MDSS_AHB_CBCR		0x4d07c
#define GCC_MDSS_AXI_CBCR		0x4d080

void clock_init(void);
void clock_enable_branch(uint32_t cbcr_offset);
void clock_enable_vote(uint32_t cbcr_offset, uint32_t vote_bit);

#endif /* __SOC_QUALCOMM_MSM89X7_CLOCK_H__ */
