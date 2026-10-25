/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __SOC_QUALCOMM_MSM89X7_ADDRESSMAP_H__
#define __SOC_QUALCOMM_MSM89X7_ADDRESSMAP_H__

/* Peripherals relevant for coreboot. Clocks, pin muxing and PMIC are
 * already set up by the primary bootloader. */

#define TLMM_BASE			0x01000000
#define GCC_BASE			0x01800000

/* BLSP1 UART2, the debug UART on most msm89x7 devices (uartdm v1.4) */
#define BLSP1_UART2_BASE		0x078b0000

/* Shared memory used by RPM/modem/TZ, must not be overwritten */
#define SMEM_BASE			0x86300000
#define SMEM_SIZE			0x200000

#endif /* __SOC_QUALCOMM_MSM89X7_ADDRESSMAP_H__ */
