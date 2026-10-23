/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/io.h>
#include <device/mmio.h>
#include <device/pci_ops.h>
#include <device/pci_def.h>
#include <device/pci_ids.h>
#include <pc80/mc146818rtc.h>

#include "pch.h"

#define GCS_ALTERNATE_ACCESS BIT(4)

/* Wellsburg RTC state-machine recovery must precede memory initialization. */
void early_pch_init_grantley(void)
{
	const u32 id = pci_read_config32(PCH_LPC_DEV, PCI_VENDOR_ID);

	if ((id & 0xffff) != PCI_VID_INTEL)
		return;

	switch (id >> 16) {
	case PCI_DID_INTEL_WBG_LPC_C610:
	case PCI_DID_INTEL_WBG_LPC_C612:
	case PCI_DID_INTEL_WBG_LPC_X99:
	case PCI_DID_INTEL_WBG_LPC_X99_ALT:
		break;
	default:
		return;
	}

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
}
