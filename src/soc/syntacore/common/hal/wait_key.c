/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <console/uart.h>
#include <soc/hal.h>
#include <device/mmio.h>
#include <drivers/uart/uart8250reg.h>

/* The generic 8250 driver exposes RX, but not its readiness accessor. */
static bool uart_can_rx_byte(unsigned int idx)
{
	return read32(uart_platform_baseptr(idx) + UART8250_LSR * sizeof(uint32_t)) &
	       UART8250_LSR_DR;
}

void scr_wait_key(void)
{
	if (!scr_mrt_was_uninitialized)
		return;

	while (uart_can_rx_byte(CONFIG_UART_FOR_CONSOLE))
		uart_rx_byte(CONFIG_UART_FOR_CONSOLE);
	printk(BIOS_NOTICE, "SCR: MRT initialized. Load DDR, then Press any key to continue.\n");
	while (!uart_can_rx_byte(CONFIG_UART_FOR_CONSOLE))
		;
	uart_rx_byte(CONFIG_UART_FOR_CONSOLE);
}
