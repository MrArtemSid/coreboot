/* SPDX-License-Identifier: GPL-2.0-only */

#include <northbridge/intel/grantley/platform.h>

/* Fixture only: exercise both fields of the optional mainboard override. */
bool mainboard_get_board_identity(struct grantley_board_identity *identity)
{
	*identity = (struct grantley_board_identity) {
		.board_id = 0x14,
		.policy_revision = 1,
	};
	return true;
}
