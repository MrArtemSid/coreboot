/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <drivers/iccm.h>

int scr_iccm_send(unsigned int slot)
{
	return iccm_send(slot, 1);
}

int scr_iccm_clear(void)
{
	for (unsigned int i = 0; i < 32; i++) {
		if (iccm_get_data() < 0)
			return 1;
	}
	return 0;
}
