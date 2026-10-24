/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/uart.h>
#include <boot/coreboot_tables.h>
#include <soc/addressmap.h>

/*
 * coreboot UART driver backed by SCR-HAL (3rdparty/scr-hal).
 *
 * All UART specifics - register layout, base address and, most
 * importantly, the clock used to compute the divisor - live in the
 * SCR-HAL platform headers and drivers. scr_hwinfo_get_uart_clk()
 * reads the live UART clock from the board (FPGA MMIO on Syntacore
 * systems), so coreboot never hardcodes a baud-rate divisor.
 */

#include "drivers/console.h"
#include "drivers/hwinfo/hwinfo.h"
#include <soc/scr_hal.h>

#define SCR_UART0_BASE	scr_hal_uart_base()

void uart_init(unsigned int idx)
{
	if (idx != CONFIG_UART_FOR_CONSOLE)
		return;

	/* SCR-HAL reads the real UART clock via its hwinfo layer. */
	scr_hwinfo_init();
	con_init();
}

void uart_tx_byte(unsigned int idx, unsigned char data)
{
	if (idx != CONFIG_UART_FOR_CONSOLE)
		return;
	con_putc(data);
}

unsigned char uart_rx_byte(unsigned int idx)
{
	if (idx != CONFIG_UART_FOR_CONSOLE)
		return 0xff;
	return (unsigned char)con_getc();
}

void uart_tx_flush(unsigned int idx)
{
	if (idx != CONFIG_UART_FOR_CONSOLE)
		return;
	con_flush();
}

uintptr_t uart_platform_base(unsigned int idx)
{
	if (idx != CONFIG_UART_FOR_CONSOLE)
		return 0;
	return SCR_UART0_BASE;
}

unsigned int uart_platform_refclk(void)
{
	/* Live UART clock from the SCR-HAL hwinfo layer. */
	return (unsigned int)scr_hwinfo_get_uart_clk();
}

#if ENV_RAMSTAGE
enum cb_err fill_lb_serial(struct lb_serial *serial)
{
	serial->type = LB_SERIAL_TYPE_MEMORY_MAPPED;
	serial->baseaddr = SCR_UART0_BASE;
	serial->baud = CONFIG_TTYS0_BAUD;
	serial->regwidth = scr_hal_uart_regwidth();
	serial->input_hertz = uart_platform_refclk();
	return CB_SUCCESS;
}
#endif
