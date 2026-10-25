/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __SOC_SYNTACORE_SCR_HAL_H__
#define __SOC_SYNTACORE_SCR_HAL_H__

#include <stdint.h>

/*
 * Functions exported by scr_hal_glue.c, the only place where SCR-HAL
 * platform headers meet coreboot (through a plain function interface).
 */

void scr_hal_mtimer_enable(void);
uintptr_t scr_hal_mtimer_time_addr(void);
uintptr_t scr_hal_mtimer_cmp_addr(unsigned long hart);
uint64_t scr_hal_mtimer_now(void);
unsigned long scr_hal_timebase_hz(void);
uintptr_t scr_hal_uart_base(void);
unsigned int scr_hal_uart_regwidth(void);
void scr_hal_pmp_platform_init(void);
void scr_hal_mrt_init(void);
void scr_hal_plic_set_level_high(unsigned int irq);

void scr_hal_iccm_init(void);
int scr_hal_iccm_clear(void);
int scr_hal_iccm_send(unsigned int slot);
void scr_iccm_init(void);
unsigned int scr_hal_iccm_hart_count(void);

#endif /* __SOC_SYNTACORE_SCR_HAL_H__ */
