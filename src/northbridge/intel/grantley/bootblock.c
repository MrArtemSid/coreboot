/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/bootblock.h>
#include <cpu/cpu.h>
#include <device/pci_def.h>
#include <device/pci_ops.h>
#include <halt.h>
#include <northbridge/intel/grantley/platform.h>
#include <post.h>
#include <southbridge/intel/lynxpoint/pch.h>

void bootblock_early_northbridge_init(void)
{
	const struct cpuid_result vendor = cpuid(0);
	const uint32_t pch = pci_s_read_config32(PCH_LPC_DEV, PCI_VENDOR_ID);

	/* Check identity via PCI type 1 before enabling any PCH MMIO/decode. */
	if (vendor.ebx != 0x756e6547 || vendor.edx != 0x49656e69 ||
	    vendor.ecx != 0x6c65746e ||
	    grantley_cpu_model(cpuid_eax(1)) == GRANTLEY_CPU_UNKNOWN ||
	    !grantley_pch_is_supported(pch & 0xffff, pch >> 16)) {
		post_code(0xe0);
		halt();
	}
	/* No client PCIEXBAR write: EP address-map registers are on device 5. */
}
