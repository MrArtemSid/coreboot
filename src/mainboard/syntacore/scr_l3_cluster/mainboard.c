/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <device/device.h>
#include <symbols.h>
#include <cbmem.h>
#include <soc/addressmap.h>
#include <soc/scr_hal.h>

/*
 * RAM declaration, SiFive-style: the resource allocator derives the
 * OS-visible RAM map from the resources declared on the mainboard
 * device, so the payload staging area (all of DDR up to cbmem top)
 * has to be reported here.
 */
static void scr_mainboard_init(struct device *dev)
{
	ram_from_to(dev, 0, (uintptr_t)_dram, cbmem_top());

	if (CONFIG(SCR9_L3_QEMU_VIRTIO)) {
		/*
		 * Set Syntacore PLIC source modes to level-high for UART and
		 * the QEMU virtio transports (IRQs 240..247).
		 */
		scr_hal_plic_set_level_high(60); /* UART0 IRQ */
		for (int irq = 240; irq <= 247; irq++)
			scr_hal_plic_set_level_high(irq);
	}
}

struct chip_operations mainboard_ops = {
	.name = "Syntacore " CONFIG_MAINBOARD_PART_NUMBER,
	.enable_dev = scr_mainboard_init,
};
