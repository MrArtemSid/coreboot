/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __SOC_QUALCOMM_MSM89X7_DISPLAY_CONSOLE_H__
#define __SOC_QUALCOMM_MSM89X7_DISPLAY_CONSOLE_H__

#include <stddef.h>
#include <stdint.h>

/* Region the display console draws to (detected from the live MDP
 * configuration). Zero size if nothing was detected. Only meaningful
 * with CONFIG_MSM89X7_DISPLAY_CONSOLE enabled. */
void display_console_fb_region(uint64_t *base, size_t *size);

/* Stop the display console and blank the panel. No-op if it is not
 * active. Only meaningful with CONFIG_MSM89X7_DISPLAY_CONSOLE enabled. */
void display_console_clear(void);

#endif /* __SOC_QUALCOMM_MSM89X7_DISPLAY_CONSOLE_H__ */
