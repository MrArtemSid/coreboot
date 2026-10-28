/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <drivers/iccm_l3.h>

void scr_ipi_init(void)
{
	iccm_l3_write_b_control(SCR_ICCM_ICCM_CONTROL_EWE_BIT);
}

int scr_iccm_send(unsigned int slot)
{
	if (slot >= 64 || iccm_l3_receiver_busy(iccm_l3_get_own_id(), slot))
		return 0;
	iccm_l3_write_msg(slot, 1);
	return 1;
}

int scr_iccm_clear(void)
{
	for (unsigned int i = 0; i < 64; i++) {
		if (!ipi_l3_msg_available())
			return 1;
		iccm_l3_read_msg();
	}
	return 0;
}
