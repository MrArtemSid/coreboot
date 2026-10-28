/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SOC_SYNTACORE_SCR_HAL_H
#define SOC_SYNTACORE_SCR_HAL_H

#include <stdbool.h>

void scr_hart_init(void);
void scr_wait_key(void);
extern bool scr_mrt_was_uninitialized;

void scr_hal_early_init(void);
void scr_hal_cache_shared_init(void);
void scr_hal_cache_local_init(void);
void scr_hal_memory_init(void);
int scr_hal_mrt_is_initialized(void);
void scr_hal_mrt_init(void);
void scr_hal_features_init(void);
void scr_hal_timer_init(void);

#endif
