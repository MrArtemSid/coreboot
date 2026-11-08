/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <acpi/acpi.h>
#include <acpi/acpigen.h>
#include <commonlib/helpers.h>
#include "grantley.h"

#define MWAIT_RES(state, sub_state) { \
	.addrl = (((state) << 4) | (sub_state)), \
	.space_id = ACPI_ADDRESS_SPACE_FIXED, \
	.bit_width = ACPI_FFIXEDHW_VENDOR_INTEL, \
	.bit_offset = ACPI_FFIXEDHW_CLASS_MWAIT, \
	.access_size = ACPI_FFIXEDHW_FLAG_HW_COORD, \
}

/*
 * The stock firmware hands out a single C1 state from the table that _CST
 * returns, and keeps its C3 and C6 entries (MWAIT 0x10 and 0x20, 15 and 41
 * us) in a branch that is not reached. Those latencies are far below what
 * the package needs to come back, and with the deeper states advertised the
 * OS enters C6 between short bursts of device traffic: a USB transfer then
 * runs at a fraction of its speed. Advertise what stock advertises.
 */
static const acpi_cstate_t cstate_map[] = {
	{
		.ctype = 1,
		.latency = 1,
		.power = 0x3e8,
		.resource = MWAIT_RES(0, 0),
	},
};

void grantley_generate_c_state_entries(void)
{
	acpigen_write_CST_package(cstate_map, ARRAY_SIZE(cstate_map));
}
