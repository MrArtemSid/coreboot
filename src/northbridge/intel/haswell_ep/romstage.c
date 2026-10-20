/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <cpu/cpu.h>
#include <device/pci_def.h>
#include <device/pci_ops.h>
#include <post.h>
#include <romstage_common.h>
#include "platform.h"

void __noreturn romstage_main(void)
{
	uint32_t signature = cpuid_eax(1);
	uint32_t host = pci_s_read_config32(PCI_DEV(0, 0, 0), PCI_VENDOR_ID);
	uint32_t pch = pci_s_read_config32(PCI_DEV(0, 0x1f, 0), PCI_VENDOR_ID);

	printk(BIOS_INFO, "Grantley diagnostic: CPUID %08x, host %08x, LPC %08x\n",
	       signature, host, pch);
	printk(BIOS_INFO, "CPU model: %s\n",
	       grantley_cpu_model(signature) == GRANTLEY_HASWELL_EP ?
	       "Haswell-EP" : "Broadwell-EP (not hardware-validated)");

	/* Do not invoke the client MRC, train DRAM, publish CBMEM or leave CAR. */
	post_code(0xe3);
	die("Grantley: stopping before DRAM init; Uncore PEI contract not implemented.\n");
}
