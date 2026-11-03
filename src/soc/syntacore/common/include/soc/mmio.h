/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SOC_SYNTACORE_SCR_MMIO_H
#define SOC_SYNTACORE_SCR_MMIO_H

#include <stdint.h>

/*
 * Addresses of the on-chip peripheral registers. SCR-HAL owns the platform
 * memory map, so the values are not repeated here: hal/mmio_hal.c maps them
 * from the PLF_* macros of the platform header selected by
 * CONFIG_SCR_HAL_PLATFORM.
 *
 * SCR-HAL headers cannot be included from coreboot code — their arch.h and
 * csr.h redefine coreboot's encoding.h and STRINGIFY — so this interface is
 * a set of plain calls compiled with the SCR-HAL include path.
 */
uintptr_t scr_uart0_base(void);
uintptr_t scr_sysclk_mhz_addr(void);
uintptr_t scr_mtimer_base(void);
/* Present only on platforms whose SCR-HAL header describes a PLIC. */
uintptr_t scr_plic_base(void);

#endif
