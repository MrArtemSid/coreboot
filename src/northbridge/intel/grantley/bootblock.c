/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/bootblock.h>

void bootblock_early_northbridge_init(void)
{
	/* No client PCIEXBAR write: EP address-map registers are on device 5. */
}
