/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <bootstate.h>
#include <console/console.h>
#include <cpu/intel/grantley/grantley_routing.h>
#include <delay.h>
#include <device/pci_def.h>
#include <device/pci_ids.h>
#include <device/pci_ops.h>
#include <timer.h>

#define PCU_BIOS_RESET_CPL	0x94
#define PCU_RST_CPL1		BIT(1)
#define PCU_RST_CPL2		BIT(2)

/* Adapted from soc/intel/xeon_sp/pcu1.c. */
static void set_bios_reset_cpl(pci_devfn_t pcu, unsigned int phase)
{
	const u32 request = BIT(phase);
	const u32 ack = BIT(phase + 8);
	struct stopwatch sw;
	u32 value = pci_s_read_config32(pcu, PCU_BIOS_RESET_CPL);

	if (value == UINT32_MAX)
		die("Grantley: PCU is inaccessible\n");

	pci_s_write_config32(pcu, PCU_BIOS_RESET_CPL, value | request);
	stopwatch_init_msecs_expire(&sw, 5000);
	for (;;) {
		value = pci_s_read_config32(pcu, PCU_BIOS_RESET_CPL);
		if (value != UINT32_MAX && (value & ack))
			return;
		if (stopwatch_expired(&sw)) {
			printk(BIOS_ERR, "Grantley: CPL%u timed out, BIOS_RESET_CPL=%08x\n",
			       phase, value);
			die("Grantley: PCU did not acknowledge BIOS init completion\n");
		}
		udelay(50);
	}
}

static void grantley_bios_init_completion(void *unused)
{
	const u8 bus = grantley_uncore_bus();
	const pci_devfn_t pcu = PCI_DEV(bus, 30, 1);
	const u32 id = pci_s_read_config32(pcu, PCI_VENDOR_ID);
	u32 value;

	if (id != ((0x2f99U << 16) | PCI_VID_INTEL) &&
	    id != ((0x6f99U << 16) | PCI_VID_INTEL))
		die("Grantley: PCU not found on the active uncore bus\n");

	value = pci_s_read_config32(pcu, PCU_BIOS_RESET_CPL);
	printk(BIOS_DEBUG, "Grantley: %02x:1e.1 BIOS_RESET_CPL=%08x\n", bus, value);
	if (value == UINT32_MAX ||
	    (value & (PCU_RST_CPL1 | PCU_RST_CPL2)) != (PCU_RST_CPL1 | PCU_RST_CPL2))
		die("Grantley: Uncore initialization is incomplete\n");

	set_bios_reset_cpl(pcu, 3);
	/* CPL4 locks the BIOS mailbox; issue it only after PM configuration. */
	set_bios_reset_cpl(pcu, 4);

	printk(BIOS_DEBUG, "Grantley: BIOS_RESET_CPL=%08x after PM handoff\n",
	       pci_s_read_config32(pcu, PCU_BIOS_RESET_CPL));
}

BOOT_STATE_INIT_ENTRY(BS_WRITE_TABLES, BS_ON_ENTRY, grantley_bios_init_completion, NULL);
BOOT_STATE_INIT_ENTRY(BS_OS_RESUME, BS_ON_ENTRY, grantley_bios_init_completion, NULL);
