/* SPDX-License-Identifier: GPL-2.0-only */

#include <northbridge/intel/grantley/platform.h>

bool mainboard_get_board_identity(struct grantley_board_identity *identity)
{
	/* Fixed stock PlatformInfo policy: one DIMM per channel, revision zero. */
	*identity = (struct grantley_board_identity) {
		.board_id = 0x13,
		.policy_revision = 0,
	};
	return true;
}
