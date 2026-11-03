/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/encoding.h>
#include <mcall.h>
#include <soc/ipi.h>

void scr_ipi_send(unsigned int hart)
{
	scr_imsic_send(hart);
}

/* Drop a pending IPI and arm the receiver before the hart sleeps. */
void scr_ipi_clear(void)
{
	scr_imsic_arm();
}

/* IMSIC delivers IPIs as machine external interrupts. */
unsigned long riscv_ipi_mask(void)
{
	return MIP_MEIP;
}
