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

/* Xeon SP hints and latencies; Grantley has no C7. */
static const acpi_cstate_t cstate_map[] = {
	{
		.ctype = 1,
		.latency = 1,
		.power = 0x3e8,
		.resource = MWAIT_RES(0, 0),
	},
	{
		.ctype = 2,
		.latency = 15,
		.power = 0x1f4,
		.resource = MWAIT_RES(1, 0),
	},
	{
		/* Stock reports both deeper MWAIT states as ACPI C2. */
		.ctype = 2,
		.latency = 41,
		.power = 0x15e,
		.resource = MWAIT_RES(2, 0),
	},
};

void grantley_generate_c_state_entries(void)
{
	acpigen_write_CST_package(cstate_map, ARRAY_SIZE(cstate_map));
}
