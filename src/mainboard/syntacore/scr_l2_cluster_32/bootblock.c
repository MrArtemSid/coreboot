/* SPDX-License-Identifier: GPL-2.0-only */

#include <bootblock_common.h>
#include <cbfs.h>
#include <console/console.h>
#include <commonlib/device_tree.h>
#include <mcall.h>
#include <soc/scr_hal.h>

/*
 * SCR QEMU machines enter the firmware with a0 = hartid and a1 = 0:
 * no device tree pointer is handed over (unlike QEMU virt). The board
 * device tree is therefore compiled into CBFS (RISCV_DTS) and installed
 * as the hart's FDT here. It propagates to romstage/ramstage through
 * the coreboot stage handoff (a1) and is finally passed to OpenSBI.
 */
void bootblock_mainboard_init(void)
{
	size_t size = 0;
	void *fdt = cbfs_ro_map("DTB", &size);

	if (!fdt) {
		printk(BIOS_ERR, "SCR: no 'dtb' file in CBFS\n");
		return;
	}

	if (!fdt_is_valid(fdt)) {
		printk(BIOS_ERR, "SCR: invalid FDT magic at %p\n", fdt);
		cbfs_unmap(fdt);
		return;
	}

	HLS()->fdt = fdt;
	printk(BIOS_DEBUG, "SCR: FDT @ %p (%zu bytes)\n", fdt, size);

	/* miniboot-style cluster unlock before any payload takes over */
	scr_hal_mrt_init();
	scr_hal_pmp_platform_init();
}
