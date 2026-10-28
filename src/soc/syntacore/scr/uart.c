/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/uart.h>
#include <device/mmio.h>
#include <soc/addressmap.h>

uintptr_t uart_platform_base(unsigned int idx)
{
	return idx ? 0 : SCR_UART0_BASE;
}

unsigned int uart_platform_refclk(void)
{
	unsigned int mhz = read32((void *)SCR_SYSCLK_MHZ);

	return (mhz ? mhz : 50) * 1000000;
}
