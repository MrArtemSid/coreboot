/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SOC_SYNTACORE_COMMON_SOC_H
#define SOC_SYNTACORE_COMMON_SOC_H

struct device;

/* Chip-level device enable shared by Syntacore SoCs. */
void syntacore_soc_enable(struct device *dev);

#endif
