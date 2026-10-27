/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _SOC_QUALCOMM_MSM89X7_CLOCK_H_
#define _SOC_QUALCOMM_MSM89X7_CLOCK_H_

#include <types.h>

/* Enable BLSP1 AHB and the BLSP1 UART2 core clock at 7.3728 MHz. */
enum cb_err msm89x7_clock_enable_uart(void);

#endif /* _SOC_QUALCOMM_MSM89X7_CLOCK_H_ */
