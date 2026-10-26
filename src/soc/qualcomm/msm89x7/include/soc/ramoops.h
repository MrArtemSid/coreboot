/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _SOC_MSM89X7_RAMOOPS_H_
#define _SOC_MSM89X7_RAMOOPS_H_

#include <stdint.h>

/* IMEM restart reason register, read by lk2nd's check_reboot_mode(). */
#define MSM89X7_RESTART_REASON_ADDR	0x0860065c
#define MSM89X7_RESTART_REASON_FASTBOOT	0x77665500

/* Free IMEM word next to the restart reason: the display console keeps
 * its cursor here so romstage/ramstage continue after the bootblock
 * instead of redrawing over it (each stage has its own BSS). */
#define MSM89X7_IMEM_CURSOR_ADDR	0x08600660

/* APSS watchdog (32.768 kHz sleep clock). */
#define MSM89X7_WDT_BASE		0x0b017000

void msm89x7_wdt_arm(unsigned int ms);
void msm89x7_wdt_pet(void);
void msm89x7_wdt_disable(void);

#endif
