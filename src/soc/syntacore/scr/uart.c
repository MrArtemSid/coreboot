/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/uart.h>
#include <device/mmio.h>
#include <soc/mmio.h>

uintptr_t uart_platform_base(unsigned int idx)
{
	return idx ? 0 : scr_uart0_base();
}

unsigned int uart_platform_refclk(void)
{
	unsigned int mhz = read32((void *)scr_sysclk_mhz_addr());

	return (mhz ? mhz : 50) * 1000000;
}
