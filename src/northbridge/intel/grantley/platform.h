/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NORTHBRIDGE_INTEL_GRANTLEY_PLATFORM_H
#define NORTHBRIDGE_INTEL_GRANTLEY_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

struct grantley_board_identity {
	uint8_t board_id;
	uint32_t policy_revision;
};

/* Return false to preserve the default PlatformInfo policy. */
bool mainboard_get_board_identity(struct grantley_board_identity *identity);

/* PCH compatibility check for Grantley platform (Lynx Point, Wildcat Point, Wellsburg). */
bool grantley_pch_is_supported(uint16_t vendor, uint16_t device);

#endif
