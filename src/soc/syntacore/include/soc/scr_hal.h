/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __SOC_SYNTACORE_SCR_HAL_H__
#define __SOC_SYNTACORE_SCR_HAL_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

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
void scr_hal_pmp_early_init(void);
void scr_hal_pmp_platform_init(void);
void scr_hal_cache_shared_init(void);
void scr_hal_cache_local_init(void);
void scr_hal_cache_sync_code(uintptr_t start, size_t size);
void scr_hal_mrt_init(void);
int scr_hal_mrt_is_initialized(void);
void scr_hal_wait_key_before_ddr(void);

/*
 * Set to true in the bootblock when this boot actually performed the
 * cluster MRT initialization (i.e. cold reset / first power-up); left
 * untouched (false) when MRT was already initialized. Consumed before
 * the RAM stage to decide whether to wait for a terminal keypress
 * before touching DDR (see src/soc/syntacore/romstage.c).
 */
extern bool scr_is_cold_reset;
void scr_hal_plic_set_level_high(unsigned int irq);

void scr_hal_iccm_init(void);
int scr_hal_iccm_clear(void);
int scr_hal_iccm_send(unsigned int slot);
void scr_iccm_init(void);
unsigned int scr_hal_iccm_hart_count(void);

#endif /* __SOC_SYNTACORE_SCR_HAL_H__ */
