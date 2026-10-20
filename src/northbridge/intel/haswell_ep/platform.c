/* SPDX-License-Identifier: GPL-2.0-only */

#include "platform.h"

enum grantley_cpu grantley_cpu_model(uint32_t signature)
{
	switch (signature & ~0xfu) {
	case 0x306f0:
		return GRANTLEY_HASWELL_EP;
	case 0x406f0:
		return GRANTLEY_BROADWELL_EP;
	default:
		return GRANTLEY_CPU_UNKNOWN;
	}
}

bool grantley_pch_matches(uint16_t vendor, uint16_t device, enum grantley_board board)
{
	if (vendor != 0x8086)
		return false;

	/* IDs observed in the QD4 and BD4 hardware captures, not the X99 branding. */
	switch (board) {
	case GRANTLEY_BOARD_QD4:
		return device == 0x8c4e; /* Q87 */
	case GRANTLEY_BOARD_BD4:
		return device == 0x8c50; /* B85 */
	default:
		return false;
	}
}
