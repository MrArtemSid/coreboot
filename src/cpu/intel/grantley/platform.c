/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/helpers.h>
#include "grantley.h"

enum grantley_cpu grantley_cpu_model(uint32_t signature)
{
	switch (signature & ~0xfu) {
	case 0x306f0:
		return GRANTLEY_HASWELL_EP;
	case 0x406f0:
		return GRANTLEY_BROADWELL_EP;
	default:
		return GRANTLEY_CPU_UNKNOWN;
	}
}

bool grantley_smrr_values(uint64_t base, uint64_t size, uint32_t *smrr_base,
			 uint32_t *smrr_mask)
{
	if (!smrr_base || !smrr_mask || base < 0x100000 || size < 0x1000 ||
	    size >= (1ULL << 32) || (size & (size - 1)) || (base & (size - 1)) ||
	    base >= (1ULL << 32) || size > (1ULL << 32) - base)
		return false;

	*smrr_base = (uint32_t)base | 6; /* Write-back. */
	*smrr_mask = (~((uint32_t)size - 1) & 0xfffff000) | (1 << 11);
	return true;
}
