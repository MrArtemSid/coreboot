/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/romstage.h>
#include <console/console.h>
#include <cpu/cpu.h>
#include <device/pci_def.h>
#include <device/pci_ops.h>
#include <post.h>
#include <southbridge/intel/lynxpoint/pch.h>

void mainboard_romstage_entry(void)
{
	early_pch_init();

	printk(BIOS_INFO, "Grantley diagnostic: CPUID %08x, host %08x, LPC %08x\n",
	       cpuid_eax(1),
	       pci_s_read_config32(PCI_DEV(0, 0, 0), PCI_VENDOR_ID),
	       pci_s_read_config32(PCI_DEV(0, 0x1f, 0), PCI_VENDOR_ID));

	/* Do not publish RAM or leave CAR before a memory initializer exists. */
	post_code(0xe3);
	die("Grantley: stopping before DRAM training; memory initialization unavailable.\n");
}
