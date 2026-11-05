/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NORTHBRIDGE_INTEL_GRANTLEY_CHIP_H
#define NORTHBRIDGE_INTEL_GRANTLEY_CHIP_H

#include <stdbool.h>
#include <types.h>

/* IOU bifurcation, listed from the lowest port. 0 is the reset layout. */
#define IIO_BIFURCATE_X4X4X4X4	0
#define IIO_BIFURCATE_X4X4X8	1
#define IIO_BIFURCATE_X8X4X4	2
#define IIO_BIFURCATE_X8X8	3
#define IIO_BIFURCATE_X16	4

/* IOU2 has eight lanes. */
#define IIO_BIFURCATE_X4X4	0
#define IIO_BIFURCATE_X8	1

struct northbridge_intel_grantley_config {
	bool vtd_support;
	bool start_iio_links;
	u8 iou0_bifurcation;	/* 00:02.0 */
	u8 iou1_bifurcation;	/* 00:03.0 */
	u8 iou2_bifurcation;	/* 00:01.0 */
};

#endif /* NORTHBRIDGE_INTEL_GRANTLEY_CHIP_H */
