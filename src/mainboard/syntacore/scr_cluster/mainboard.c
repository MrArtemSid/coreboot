/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/device.h>
#include <device/mmio.h>
#include <soc/addressmap.h>

static void mainboard_init(void *chip_info)
{
	if (CONFIG(SCR_CLUSTER_QEMU) && !CONFIG(SCR_IPI_IMSIC)) {
		/* Stock kernels do not configure Syntacore PLIC source modes. */
		for (unsigned int irq = 1; irq <= 287; irq++)
			write32((void *)(uintptr_t)(SCR_PLIC_BASE + 0x1f0000 + 4 * irq), 1);
	}
}

struct chip_operations mainboard_ops = {
	.init = mainboard_init,
};
