/* SPDX-License-Identifier: GPL-2.0-only */

#include <cbfs.h>
#include <cbmem.h>
#include <commonlib/device_tree.h>
#include <console/console.h>
#include <endian.h>
#include <mcall.h>
#include <program_loading.h>
#include <romstage_common.h>
#include <soc/hal.h>

static void setup_fdt(void)
{
	size_t size;
	const struct fdt_header *fdt;

	if (CONFIG(SCR_BOOT_MINIBOOT) && fdt_is_valid(HLS()->fdt))
		return;

	fdt = cbfs_ro_map("DTB", &size);
	if (!fdt || size < sizeof(*fdt) || be32_to_cpu(fdt->totalsize) > size ||
	    be32_to_cpu(fdt->totalsize) < sizeof(*fdt) || !fdt_is_valid(fdt))
		die("Invalid CBFS DTB");
	HLS()->fdt = (void *)fdt;
}

void __noreturn romstage_main(void)
{
	/* Let the host load DDR before the first access to it. */
	if (CONFIG(SCR_WAIT_KEY_ON_FIRST_BOOT))
		scr_wait_key();

	setup_fdt();
	cbmem_initialize_empty();
	run_ramstage();
}
