/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/lib_helpers.h>
#include <arch/mmu.h>
#include <cbmem.h>
#include <commonlib/helpers.h>
#include <device/mmio.h>
#include <program_loading.h>
#include <soc/display_console.h>
#include <soc/payload_fb.h>
#include <soc/ramoops.h>
#include <soc/symbols.h>

uintptr_t cbmem_top_chipset(void)
{
	return (uintptr_t)((uint64_t)CONFIG_MSM89X7_DDR_START +
			   (uint64_t)CONFIG_MSM89X7_DRAM_SIZE_MB * MiB);
}

void platform_prog_run(struct prog *prog)
{
	if (!ENV_RAMSTAGE || prog_type(prog) != PROG_PAYLOAD)
		return;

	/* The payload owns the watchdog. */
	msm89x7_wdt_disable();

	if (CONFIG(MSM89X7_CLEAR_DISPLAY_BEFORE_PAYLOAD))
		display_console_clear();

	if (CONFIG(MSM89X7_PAYLOAD_LK_DTB)) {
		void *fdt = (void *)(uintptr_t)read64((void *)_lk_dtb);

		if (CONFIG(MSM89X7_PAYLOAD_SIMPLEFB))
			fdt = msm89x7_payload_fb_handoff(fdt);
		prog_set_arg(prog, fdt);
	}

	/*
	 * There is no EL3 transition that would turn the MMU off, so the
	 * payload would otherwise inherit our whitelist page tables and
	 * fault on the first access outside of them (e.g. U-Boot relocating
	 * itself to the end of DRAM). Enter it with MMU and D-cache off,
	 * like on the other arm64 paths.
	 */
	mmu_disable();

	/*
	 * LK leaves the GIC running with interrupts routed to this CPU.
	 * Payloads (U-Boot, Linux) expect to be entered with DAIF masked.
	 */
	raw_write_daif(0x3c0);
}
