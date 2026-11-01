/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdint.h>
#include <arch.h>
#include <soc/mmio.h>

/* Export the HAL constant for the generated C/linker memory map header. */
asm(".globl _scr_hal_ddr_base\n"
	".set _scr_hal_ddr_base, " STRINGIFY(PLF_MEM_BASE));

/*
 * Peripheral register addresses of the SCR-HAL platform, see soc/mmio.h for
 * why coreboot reads them through calls instead of including SCR-HAL headers.
 */

uintptr_t scr_uart0_base(void)
{
	return (uintptr_t)PLF_UART0_BASE;
}

/* Register holding the platform clock in MHz, not the clock itself. */
uintptr_t scr_sysclk_mhz_addr(void)
{
	return (uintptr_t)PLF_SYSCLK_MHZ_ADDR;
}

uintptr_t scr_mtimer_base(void)
{
	return (uintptr_t)PLF_MTIMER_BASE;
}

/*
 * Not every SCR-HAL platform has a PLIC: platforms whose interrupts are
 * routed through the AIA do not describe one, so the accessor is compiled
 * only where the platform header defines its base.
 */
#ifdef PLF_PLIC_BASE
uintptr_t scr_plic_base(void)
{
	return (uintptr_t)PLF_PLIC_BASE;
}
#endif
