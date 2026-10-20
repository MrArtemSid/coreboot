/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NORTHBRIDGE_INTEL_HASWELL_EP_PLATFORM_H
#define NORTHBRIDGE_INTEL_HASWELL_EP_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

/* Model classification only: not a claim of validated socket/stepping support. */
enum grantley_cpu {
	GRANTLEY_CPU_UNKNOWN,
	GRANTLEY_HASWELL_EP,
	GRANTLEY_BROADWELL_EP,
};

enum grantley_board {
	GRANTLEY_BOARD_QD4,
	GRANTLEY_BOARD_BD4,
};

enum grantley_cpu grantley_cpu_model(uint32_t signature);
bool grantley_pch_matches(uint16_t vendor, uint16_t device, enum grantley_board board);

#endif
