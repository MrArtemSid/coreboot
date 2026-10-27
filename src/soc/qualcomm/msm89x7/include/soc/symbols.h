/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __SOC_QUALCOMM_MSM89X7_SYMBOLS_H_
#define __SOC_QUALCOMM_MSM89X7_SYMBOLS_H_

#include <symbols.h>

/* Pointer to the device tree handed over by the primary bootloader in
 * x0, stored by the bootblock entry code. Written as u64. */
DECLARE_REGION(lk_dtb);

#endif // __SOC_QUALCOMM_MSM89X7_SYMBOLS_H_
