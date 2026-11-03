/* SPDX-License-Identifier: GPL-2.0-only */

#include <cbfs.h>
#include <cbmem.h>
#include <commonlib/device_tree.h>
#include <console/console.h>
#include <endian.h>
#include <mcall.h>
#include <program_loading.h>
#include <romstage_common.h>
#include <soc/addressmap.h>
#include <soc/hal.h>

static void setup_fdt(void)
{
	size_t size;
	const struct fdt_header *fdt;

	if (CONFIG(SCR_BOOT_MINIBOOT) && fdt_is_valid(HLS()->fdt))
		return;

	if (CONFIG(SCR_PRELOADED_PAYLOAD)) {
		fdt = (const struct fdt_header *)(uintptr_t)SCR_PRELOADED_DTB_BASE;
		if (!fdt_is_valid(fdt))
			die("Invalid preloaded DTB header\n");

		uint32_t totalsize = be32_to_cpu(fdt->totalsize);
		if (totalsize < sizeof(*fdt) || totalsize > SCR_PRELOADED_DTB_MAX_SIZE)
			die("Preloaded DTB size out of bounds\n");

		HLS()->fdt = (void *)fdt;
		return;
	}

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
