/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/helpers.h>
#include "platform.h"

/* Default weak implementation: stock clocks and voltages, no unlock */
void __weak mainboard_cpu_init_params(struct grantley_cpu_params *params)
{
	if (!params)
		return;
	params->enable_turbo_unlock = false;
	params->core_voltage_offset_mv = 0;
	params->uncore_voltage_offset_mv = 0;
}

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

bool grantley_pch_is_supported(uint16_t vendor, uint16_t device)
{
	if (vendor != 0x8086)
		return false;

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
	/* Wellsburg C610/C612 / X99 */
	case GRANTLEY_PCH_C612:
	case GRANTLEY_PCH_X99:
		return true;
	default:
		return false;
	}
}
