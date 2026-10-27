/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <console/uart.h>
#include <delay.h>
#include <device/mmio.h>
#include <gpio.h>
#include <soc/addressmap.h>
#include <soc/clock.h>

/*
 * Polled driver for the UART_DM v1.4 block in BLSP1 (UART2, the debug
 * UART on GPIO4/GPIO5), modelled after the LK msm_shared uart_dm driver.
 * Only 115200 8N1 is supported: the core clock is 7.3728 MHz and CSR
 * selects /64 for both directions.
 */
#define UART_DM_MR1		0x000
#define UART_DM_MR2		0x004
#define  MR2_8N1		((1 << 2) | (3 << 4))
#define UART_DM_IPR		0x018
#define UART_DM_TFWR		0x01c
#define UART_DM_RFWR		0x020
#define UART_DM_HCR		0x024
#define UART_DM_DMRX		0x034
#define UART_DM_DMEN		0x03c
#define UART_DM_NCF_TX		0x040
#define UART_DM_CSR		0x0a0
#define  CSR_115200		0xcc
#define UART_DM_SR		0x0a4
#define  SR_RXRDY		BIT(0)
#define  SR_TXEMT		BIT(3)
#define  SR_OVERRUN		BIT(4)
#define UART_DM_CR		0x0a8
#define  CR_RX_EN		BIT(0)
#define  CR_TX_EN		BIT(2)
#define  CR_CMD(x)		((((x) & 0xf) << 4) | (((x) >> 4) << 11))
#define  CMD_RESET_RX		CR_CMD(1)
#define  CMD_RESET_TX		CR_CMD(2)
#define  CMD_RESET_ERR		CR_CMD(3)
#define  CMD_RESET_STALE	CR_CMD(8)
#define  CMD_RESET_TX_ERR	CR_CMD(0x10)
#define  GCMD_ENA_STALE		(5 << 8)
#define  GCMD_DIS_STALE		(6 << 8)
#define UART_DM_IMR		0x0b0
#define UART_DM_IRDA		0x0b8
#define UART_DM_TF		0x100
#define UART_DM_RF		0x140

#define TX_TIMEOUT_US		1000

static void *uart_base(void)
{
	return (void *)(uintptr_t)BLSP1_UART2_BASE;
}

static void uart_wr(unsigned int reg, u32 val)
{
	write32(uart_base() + reg, val);
}

static u32 uart_rd(unsigned int reg)
{
	return read32(uart_base() + reg);
}

static void uart_dm_rx_start(void)
{
	uart_wr(UART_DM_CR, GCMD_DIS_STALE);
	uart_wr(UART_DM_CR, CMD_RESET_STALE);
	uart_wr(UART_DM_DMRX, 0x220);
	uart_wr(UART_DM_CR, GCMD_ENA_STALE);
}

uintptr_t uart_platform_base(unsigned int idx)
{
	return BLSP1_UART2_BASE;
}

void uart_init(unsigned int idx)
{
	/* Clocks first: touching an unclocked UART_DM is a synchronous
	 * external abort. */
	if (msm89x7_clock_enable_uart() != CB_SUCCESS)
		return;

	gpio_configure(GPIO(4), GPIO4_FUNC_BLSP_UART2, GPIO_NO_PULL,
		       GPIO_8MA, GPIO_OUTPUT);
	gpio_configure(GPIO(5), GPIO5_FUNC_BLSP_UART2, GPIO_NO_PULL,
		       GPIO_8MA, GPIO_INPUT);

	uart_wr(UART_DM_CSR, CSR_115200);
	uart_wr(UART_DM_MR1, 0);
	uart_wr(UART_DM_MR2, MR2_8N1);
	uart_wr(UART_DM_IMR, 0);
	uart_wr(UART_DM_TFWR, 0);
	uart_wr(UART_DM_RFWR, 0);
	uart_wr(UART_DM_IPR, 0xf);
	uart_wr(UART_DM_IRDA, 0);
	uart_wr(UART_DM_HCR, 0);

	uart_wr(UART_DM_CR, CMD_RESET_RX);
	uart_wr(UART_DM_CR, CMD_RESET_TX);
	uart_wr(UART_DM_CR, CMD_RESET_ERR);
	uart_wr(UART_DM_CR, CMD_RESET_TX_ERR);
	uart_wr(UART_DM_CR, CMD_RESET_STALE);

	uart_wr(UART_DM_DMEN, 0);
	uart_wr(UART_DM_CR, CR_RX_EN);
	uart_wr(UART_DM_CR, CR_TX_EN);
	uart_dm_rx_start();
}

static void uart_wait_txemt(void)
{
	for (int i = 0; i < TX_TIMEOUT_US; i++) {
		if (uart_rd(UART_DM_SR) & SR_TXEMT)
			return;
		udelay(1);
	}
}

void uart_tx_byte(unsigned int idx, unsigned char data)
{
	uart_wait_txemt();
	uart_wr(UART_DM_NCF_TX, 1);
	uart_wr(UART_DM_TF, data);
}

void uart_tx_flush(unsigned int idx)
{
	uart_wait_txemt();
}

/* The RX FIFO is word based; hand out the bytes of one word in turn. */
unsigned char uart_rx_byte(unsigned int idx)
{
	static u32 word;
	static unsigned int left;
	unsigned char c;

	if (!left) {
		if (!(uart_rd(UART_DM_SR) & SR_RXRDY))
			return 0;
		if (uart_rd(UART_DM_SR) & SR_OVERRUN)
			uart_wr(UART_DM_CR, CMD_RESET_ERR);
		word = uart_rd(UART_DM_RF);
		left = 4;
	}

	c = word & 0xff;
	word >>= 8;
	left--;
	return c;
}

/* Payloads have no UART_DM driver in the coreboot table format. */
enum cb_err fill_lb_serial(struct lb_serial *serial)
{
	return CB_ERR;
}
