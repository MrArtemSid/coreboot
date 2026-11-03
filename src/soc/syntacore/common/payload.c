/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/boot.h>
#include <console/console.h>
#include <program_loading.h>
#include <soc/addressmap.h>

int prog_locate_hook(struct prog *prog)
{
	if (CONFIG(SCR_PRELOADED_PAYLOAD) && prog_type(prog) == PROG_PAYLOAD) {
		printk(BIOS_INFO, "SCR: Using preloaded payload at 0x%lx\n",
		       (unsigned long)CONFIG_SCR_PRELOADED_PAYLOAD_ENTRY);
		prog_set_entry(prog, (void *)(uintptr_t)CONFIG_SCR_PRELOADED_PAYLOAD_ENTRY, NULL);
		return 1;
	}
	return 0;
}

bool platform_get_preloaded_opensbi(struct prog *sbi)
{
	if (CONFIG(SCR_PRELOADED_PAYLOAD)) {
		printk(BIOS_INFO, "SCR: Using preloaded OpenSBI at 0x%lx\n",
		       (unsigned long)SCR_OPENSBI_BASE);
		prog_set_entry(sbi, (void *)(uintptr_t)SCR_OPENSBI_BASE, NULL);
		return true;
	}
	return false;
}
