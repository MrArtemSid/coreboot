/* SPDX-License-Identifier: GPL-2.0-only */

#include <cbmem.h>
#include <device/device.h>
#include <soc/addressmap.h>
#include <soc/soc.h>

void syntacore_soc_enable(struct device *dev)
{
	ram_from_to(dev, 0, SCR_DDR_BASE, cbmem_top());
	if (CONFIG(SCR_PRELOADED_PAYLOAD))
		reserved_ram_from_to(dev, 1, SCR_OPENSBI_BASE, SCR_OPENSBI_BASE + SCR_OPENSBI_SIZE);
}
