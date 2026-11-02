/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NORTHBRIDGE_INTEL_GRANTLEY_SPD_H
#define NORTHBRIDGE_INTEL_GRANTLEY_SPD_H

#include <device/dram/ddr4.h>

enum grantley_spd_status {
	GRANTLEY_SPD_OK,
	GRANTLEY_SPD_IO_ERROR,
	GRANTLEY_SPD_INVALID,
	GRANTLEY_SPD_UNSUPPORTED,
};

/* Read-only DIMM inventory. Success does not establish training compatibility. */
enum grantley_spd_status grantley_read_spd(uintptr_t base, uint8_t address,
					 struct dimm_attr_ddr4_st *dimm);

#endif /* NORTHBRIDGE_INTEL_GRANTLEY_SPD_H */
