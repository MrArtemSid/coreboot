/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <arch.h>

/*
 * SCR-HAL platform headers do not describe AIA yet. Fall back to the
 * SC-DT QEMU memory map; FPGA addresses are not verified.
 */
#ifndef PLF_APLIC_BASE_M
#if CONFIG(SCR_CLUSTER_L3)
#define PLF_APLIC_BASE_M	0x04030000UL
#define PLF_APLIC_BASE_S	0x04034000UL
#define PLF_IMSIC_BASE_M	0x11000000UL
#define PLF_IMSIC_BASE_S	0x12000000UL
#else
#define PLF_APLIC_BASE_M	0xffffff0c000000UL
#define PLF_APLIC_BASE_S	0xffffff0d000000UL
#define PLF_IMSIC_BASE_M	0xffffff24000000UL
#define PLF_IMSIC_BASE_S	0xffffff28000000UL
#endif
#define PLF_APLIC_NUM_SOURCE	287
#define PLF_IMSIC_NUM_IDS	255
#endif

#include <drivers/aia.h>

/* Match OpenSBI's machine-level IPI identity. */
#define SCR_IMSIC_IPI	1

void scr_imsic_send(unsigned int hart)
{
	fence();
	imsic_set_seteipnum_le(IMSIC_M, hart, SCR_IMSIC_IPI);
}

void scr_imsic_arm(void)
{
	imsic_write(IMSIC_M, IMSIC_EIDELIVERY, 0);
	imsic_clr_eipnum(IMSIC_M, SCR_IMSIC_IPI);
	imsic_write(IMSIC_M, IMSIC_EITHRESHOLD, 0);
	imsic_set_eienum(IMSIC_M, SCR_IMSIC_IPI);
	imsic_write(IMSIC_M, IMSIC_EIDELIVERY, 1);
}
