/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <soc/ipi.h>

void scr_ipi_send(unsigned int hart)
{
	if (!scr_iccm_send(hart))
		die("ICCM send failed");
}

void scr_ipi_clear(void)
{
	if (!scr_iccm_clear())
		die("ICCM drain failed");
}
