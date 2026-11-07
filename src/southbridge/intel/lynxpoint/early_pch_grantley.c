/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/io.h>
#include <console/console.h>
#include <device/mmio.h>
#include <device/pci_ops.h>
#include <device/pci_def.h>
#include <device/pci_ids.h>
#include <northbridge/intel/grantley/platform.h>
#include <pc80/mc146818rtc.h>

#include "pch.h"
#include "hsio/hsio.h"

#define GCS_ALTERNATE_ACCESS BIT(4)

/*
 * Device 0x11 hosts the PCH MS unit (8086:8d7c, named MROM in coreboot's
 * own devicetrees). Its function-hide register controls the functions of
 * that device, including the second storage complex at 0x11.4 / 0x11.5.
 */
#define PCH_MSUNIT_DEVFN	PCI_DEV(0, 0x11, 0)
#define MSUNIT_DEVFUNCHIDE	0xd4
#define MSUNIT_HIDE_SSATA	BIT(4)
#define MSUNIT_HIDE_SSATA2	BIT(5)

/*
 * The second storage complex is not wired on this class of board, yet it is
 * still enumerated after reset and Linux binds ata_piix to four ports that it
 * cannot reach. Both of its functions can be taken off the bus by their owner,
 * which leaves nothing for the payload or the OS to probe.
 */
static void ssata_disable(void)
{
	if (pci_read_config16(PCH_MSUNIT_DEVFN, PCI_VENDOR_ID) == 0xffff) {
		printk(BIOS_DEBUG, "sSATA: MS unit absent, cannot hide it\n");
		return;
	}

	pci_or_config32(PCH_MSUNIT_DEVFN, MSUNIT_DEVFUNCHIDE,
			MSUNIT_HIDE_SSATA | MSUNIT_HIDE_SSATA2);
}

/* RTC state-machine recovery must precede memory initialization. */
void early_pch_init_grantley(void)
{
	const u32 id = pci_read_config32(PCH_LPC_DEV, PCI_VENDOR_ID);

	/* Also needed for client Lynx Point PCHs on HEDT boards (QD4: Q87). */
	if (!grantley_pch_is_supported(id & 0xffff, id >> 16))
		return;

	if (pci_read_config8(PCH_LPC_DEV, GEN_PMCON_3) & RTC_BATTERY_DEAD) {
		void *gcs = (void *)(uintptr_t)(CONFIG_FIXED_RCBA_MMIO_BASE + GCS);
		const u32 saved_gcs = read32(gcs);

		/* The RTC index/NMI mask is readable only in alternate access mode. */
		write32(gcs, saved_gcs | GCS_ALTERNATE_ACCESS);
		(void)read32(gcs);
		const u8 index = inb(RTC_BASE_PORT_BANK0);
		const u8 nmi = index & 0x80;
		write32(gcs, saved_gcs);
		(void)read32(gcs);

		outb(nmi | RTC_REG_A, RTC_BASE_PORT_BANK0);
		u8 value = inb(RTC_BASE_PORT_BANK0 + 1) & ~RTC_DIV_CTL;
		outb(value | RTC_DIV_RESET1, RTC_BASE_PORT_BANK0 + 1);

		outb(nmi | RTC_REG_B, RTC_BASE_PORT_BANK0);
		value = inb(RTC_BASE_PORT_BANK0 + 1);
		outb(value | RTC_SET, RTC_BASE_PORT_BANK0 + 1);

		outb(nmi | RTC_REG_A, RTC_BASE_PORT_BANK0);
		value = inb(RTC_BASE_PORT_BANK0 + 1) & ~RTC_DIV_CTL;
		outb(value | RTC_REF_CLCK_32KHZ, RTC_BASE_PORT_BANK0 + 1);

		outb(nmi | RTC_REG_B, RTC_BASE_PORT_BANK0);
		value = inb(RTC_BASE_PORT_BANK0 + 1);
		outb(value & ~RTC_SET, RTC_BASE_PORT_BANK0 + 1);
		outb(index, RTC_BASE_PORT_BANK0);

		/* Leave RTC_BATTERY_DEAD set for sb_rtc_init() and CMOS defaults. */
	}

	/* Clear only the early W1C status, preserving ME/reset/wake evidence. */
	write32((void *)(uintptr_t)(CONFIG_FIXED_RCBA_MMIO_BASE + PRSTS), BIT(4));

	program_hsio_sata_wbg_b0();
	if (CONFIG(WELLSBURG_DISABLE_SSATA))
		ssata_disable();

	/* The native path reaches this through early_pch_init_native(). */
	early_thermal_init();
}
