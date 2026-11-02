/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/helpers.h>
#include "platform.h"

bool grantley_pch_is_supported(uint16_t vendor, uint16_t device)
{
	if (vendor != 0x8086)
		return false;

	/* Wellsburg C610/C612 / X99 family (0x8d40..0x8d5f) */
	if (device >= GRANTLEY_PCH_WBG_MIN && device <= GRANTLEY_PCH_WBG_MAX)
		return true;

	switch (device) {
	/* Lynx Point 8-series desktop & server */
	case GRANTLEY_PCH_Z87:
	case GRANTLEY_PCH_H87:
	case GRANTLEY_PCH_Q85:
	case GRANTLEY_PCH_Q87:
	case GRANTLEY_PCH_B85:
	case GRANTLEY_PCH_C222:
	case GRANTLEY_PCH_C224:
	case GRANTLEY_PCH_C226:
	case GRANTLEY_PCH_H81:
	/* Wildcat Point 9-series */
	case GRANTLEY_PCH_Z97:
	case GRANTLEY_PCH_H97:
		return true;
	default:
		return false;
	}
}
