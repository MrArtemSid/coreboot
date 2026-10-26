/* SPDX-License-Identifier: GPL-2.0-only */

#include <bootblock_common.h>
#include <cbfs.h>
#include <console/console.h>
#include <commonlib/device_tree.h>
#include <endian.h>
#include <halt.h>
#include <mcall.h>
#include <soc/scr_hal.h>

/*
 * SCR QEMU machines enter the firmware with a0 = hartid and a1 = 0:
 * no device tree pointer is handed over (unlike QEMU virt). The board
 * device tree is therefore compiled into CBFS (RISCV_DTS) and installed
 * as the hart's FDT here. It propagates to romstage/ramstage through
 * the coreboot stage handoff (a1) and is finally passed to OpenSBI.
 */

/*
 * ISA string from the misa CSR, decoded the same way miniboot's
 * sysinfo does (MXL from the two most significant bits, extensions in
 * the IEMAFDQLNCBKTPVXHSU order).
 */
static void scr_print_isa(void)
{
	static const char mxl[4][4] = { "??", "32", "64", "128" };
	static const char ext[] = "IEMAFDQLNCBKTPVXHSU";
	const char *ep;
	unsigned long misa = read_csr(misa);
	unsigned long mxl_idx = (misa >> (__riscv_xlen - 2)) & 0x3;

	if (mxl_idx > 3)
		mxl_idx = 0;

	printk(BIOS_INFO, "SCR: ISA: RV%s", mxl[mxl_idx]);
	for (ep = ext; *ep; ++ep)
		if (misa & (1UL << (*ep - 'A')))
			printk(BIOS_INFO, "%c", *ep);
	printk(BIOS_INFO, " [%08lx]\n", misa);
}

void bootblock_mainboard_init(void)
{
	size_t size = 0;
	void *fdt = cbfs_ro_map("DTB", &size);

	if (!fdt)
		die("SCR: no 'DTB' file in CBFS\n");

	if (size < sizeof(struct fdt_header) ||
	    be32toh(((const struct fdt_header *)fdt)->totalsize) > size ||
	    !fdt_is_valid(fdt))
		die("SCR: invalid or truncated FDT in CBFS\n");

	HLS()->fdt = fdt;
	printk(BIOS_DEBUG, "SCR: FDT @ %p (%zu bytes)\n", fdt, size);

	scr_print_isa();
}
