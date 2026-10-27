/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __SOC_QUALCOMM_MSM89X7_ADDRESSMAP_H__
#define __SOC_QUALCOMM_MSM89X7_ADDRESSMAP_H__

/* Peripherals relevant for coreboot. Clocks, pin muxing and PMIC are
 * already set up by the primary bootloader. */

#define TLMM_BASE			0x01000000
#define GCC_BASE			0x01800000

/* MDSS/MDP5, used to detect the live splash framebuffer configuration */
#define MDSS_MDP_BASE			0x01a00000

/* Shared memory used by RPM/modem/TZ, must not be overwritten */
#define SMEM_BASE			0x86300000
#define SMEM_SIZE			0x100000

#endif /* __SOC_QUALCOMM_MSM89X7_ADDRESSMAP_H__ */
