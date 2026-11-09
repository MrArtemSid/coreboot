/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NORTHBRIDGE_INTEL_GRANTLEY_IIO_PHY_H
#define NORTHBRIDGE_INTEL_GRANTLEY_IIO_PHY_H

#include <stdint.h>

/*
 * One read-modify-write of an IIO DFX register: the bits set in keep are
 * preserved and value is ORed in. port selects the DFX function, see
 * iio_phy_recipe.h.
 */
struct iio_phy_setting {
	uint8_t port;
	uint8_t size;
	uint16_t reg;
	uint32_t keep;
	uint32_t value;
};

void grantley_iio_phy_init(void);

#endif /* NORTHBRIDGE_INTEL_GRANTLEY_IIO_PHY_H */
