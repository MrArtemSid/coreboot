/* UART DM console driver for msm89x7.
 *
 * Heavily based on the qcs405 UART driver ("Source : APQ8064 LK boot")
 * and on the msm8937 UART DM v1.4 hardware as described in Linux'
 * drivers/tty/serial/msm_serial.c.
 *
 * The primary bootloader has already configured the pin muxing and the
 * UART clock, so this driver only reinitializes the UART DM controller
 * itself and does not touch any clocks.
 */
/* SPDX-License-Identifier: BSD-3-Clause */

#include <device/mmio.h>
#include <boot/coreboot_tables.h>
#include <console/console.h>
#include <console/uart.h>
#include <delay.h>
#include <stdint.h>
#include <soc/uart.h>
#include <soc/addressmap.h>

#define FIFO_DATA_SIZE	4

static void *uart_dm_base(void)
{
	return (void *)(uintptr_t)CONFIG_MSM89X7_UART_BASE;
}

/**
 * @brief msm_boot_uart_dm_init_rx_transfer - Init Rx transfer
 * @param uart_dm_base: UART controller base address
 */
static unsigned int msm_boot_uart_dm_init_rx_transfer(void *uart_dm_base)
{
	/* Reset receiver */
	write32(MSM_BOOT_UART_DM_CR(uart_dm_base),
		MSM_BOOT_UART_DM_CMD_RESET_RX);

	/* Enable receiver */
	write32(MSM_BOOT_UART_DM_CR(uart_dm_base),
		MSM_BOOT_UART_DM_CR_RX_ENABLE);
	write32(MSM_BOOT_UART_DM_DMRX(uart_dm_base),
		MSM_BOOT_UART_DM_DMRX_DEF_VALUE);

	/* Clear stale event */
	write32(MSM_BOOT_UART_DM_CR(uart_dm_base),
		MSM_BOOT_UART_DM_CMD_RES_STALE_INT);

	/* Enable stale event */
	write32(MSM_BOOT_UART_DM_CR(uart_dm_base),
		MSM_BOOT_UART_DM_GCMD_ENA_STALE_EVT);

	return MSM_BOOT_UART_DM_E_SUCCESS;
}

void uart_tx_byte(unsigned int idx, unsigned char data)
{
	void *base = uart_dm_base();

	/* Wait until transmit FIFO is empty. */
	while (!(read32(MSM_BOOT_UART_DM_SR(base)) &
		 MSM_BOOT_UART_DM_SR_TXEMT))
		udelay(1);
	/*
	 * TX FIFO is ready to accept new character(s). First write number of
	 * characters to be transmitted.
	 */
	write32(MSM_BOOT_UART_DM_NO_CHARS_FOR_TX(base), 1);

	/* And now write the character(s) */
	write32(MSM_BOOT_UART_DM_TF(base, 0), data);
}

/**
 * @brief msm_boot_uart_dm_reset - resets UART controller
 * @param base: UART controller base address
 */
static unsigned int msm_boot_uart_dm_reset(void *base)
{
	write32(MSM_BOOT_UART_DM_CR(base), MSM_BOOT_UART_DM_CMD_RESET_RX);
	write32(MSM_BOOT_UART_DM_CR(base), MSM_BOOT_UART_DM_CMD_RESET_TX);
	write32(MSM_BOOT_UART_DM_CR(base),
		MSM_BOOT_UART_DM_CMD_RESET_ERR_STAT);
	write32(MSM_BOOT_UART_DM_CR(base), MSM_BOOT_UART_DM_CMD_RES_TX_ERR);
	write32(MSM_BOOT_UART_DM_CR(base),
		MSM_BOOT_UART_DM_CMD_RES_STALE_INT);

	return MSM_BOOT_UART_DM_E_SUCCESS;
}

/**
 * @brief msm_boot_uart_dm_init - initializes UART controller
 * @param uart_dm_base: UART controller base address
 */
static unsigned int msm_boot_uart_dm_init(void *uart_dm_base)
{
	/* Configure UART mode registers MR1 and MR2 */
	/* Hardware flow control isn't supported */
	write32(MSM_BOOT_UART_DM_MR1(uart_dm_base), 0x0);

	/* 8-N-1 configuration: 8 data bits - No parity - 1 stop bit */
	write32(MSM_BOOT_UART_DM_MR2(uart_dm_base),
		MSM_BOOT_UART_DM_8_N_1_MODE);

	/* Configure Interrupt Mask register IMR */
	write32(MSM_BOOT_UART_DM_IMR(uart_dm_base),
		MSM_BOOT_UART_DM_IMR_ENABLED);

	/*
	 * Configure Tx and Rx watermarks configuration registers
	 * TX watermark value is set to 0 - interrupt is generated when
	 * FIFO level is less than or equal to 0
	 */
	write32(MSM_BOOT_UART_DM_TFWR(uart_dm_base),
		MSM_BOOT_UART_DM_TFW_VALUE);

	/* RX watermark value */
	write32(MSM_BOOT_UART_DM_RFWR(uart_dm_base),
		MSM_BOOT_UART_DM_RFW_VALUE);

	/* Configure Interrupt Programming Register */
	/* Set initial Stale timeout value */
	write32(MSM_BOOT_UART_DM_IPR(uart_dm_base),
		MSM_BOOT_UART_DM_STALE_TIMEOUT_LSB);

	/* Configure IRDA if required */
	/* Disabling IRDA mode */
	write32(MSM_BOOT_UART_DM_IRDA(uart_dm_base), 0x0);

	/* Configure hunt character value in HCR register */
	/* Keep it in reset state */
	write32(MSM_BOOT_UART_DM_HCR(uart_dm_base), 0x0);

	/* Issue soft reset command */
	msm_boot_uart_dm_reset(uart_dm_base);

	/* Enable/Disable Rx/Tx DM interfaces */
	/* Data Mover not currently utilized. */
	write32(MSM_BOOT_UART_DM_DMEN(uart_dm_base), 0x0);

	/* Enable transmitter */
	write32(MSM_BOOT_UART_DM_CR(uart_dm_base),
		MSM_BOOT_UART_DM_CR_TX_ENABLE);

	/* Initialize Receive Path */
	msm_boot_uart_dm_init_rx_transfer(uart_dm_base);

	return 0;
}

/**
 * @brief uart_init - initializes UART controller
 *
 * The primary bootloader already configured clocks and pins, so only
 * the controller itself is (re)initialized here.
 */
void uart_init(unsigned int idx)
{
	/* Note int idx isn't used in this driver. */
	msm_boot_uart_dm_init(uart_dm_base());
}

/* for the benefit of non-console uart init */
void msm89x7_uart_init(void)
{
	uart_init(0);
}

/**
 * @brief uart_tx_flush - waits until the TX FIFO is empty
 */
void uart_tx_flush(unsigned int idx)
{
	void *base = uart_dm_base();

	while (!(read32(MSM_BOOT_UART_DM_SR(base)) &
		 MSM_BOOT_UART_DM_SR_TXEMT))
		;
}

uint8_t uart_rx_byte(unsigned int idx)
{
	return 0;
}

enum cb_err fill_lb_serial(struct lb_serial *serial)
{
	serial->type = LB_SERIAL_TYPE_MEMORY_MAPPED;
	lb_serial_set_baseaddr(serial, (uintptr_t)CONFIG_MSM89X7_UART_BASE);
	serial->baud = get_uart_baudrate();
	serial->regwidth = 1;

	return CB_SUCCESS;
}
