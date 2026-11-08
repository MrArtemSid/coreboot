/* SPDX-License-Identifier: GPL-2.0-only */

#include <southbridge/intel/lynxpoint/pch.h>

External (\_SB.CP00, DeviceObj)

Name (_HID, EISAID ("PNP0A08"))	/* PCI Express Bus */
Name (_CID, EISAID ("PNP0A03"))	/* PCI Bus */
Name (_UID, 0)

/* PCI Device Resource Consumption */
Device (PDRC)
{
	Name (_HID, EISAID ("PNP0C02"))
	Name (_UID, 1)

	Name (PDRS, ResourceTemplate () {
		Memory32Fixed (ReadWrite, CONFIG_FIXED_RCBA_MMIO_BASE, CONFIG_RCBA_LENGTH)
		Memory32Fixed (ReadWrite, CONFIG_FIXED_MCHBAR_MMIO_BASE, 0x8000)
		Memory32Fixed (ReadWrite, CONFIG_FIXED_DMIBAR_MMIO_BASE, 0x1000)
		Memory32Fixed (ReadWrite, CONFIG_FIXED_EPBAR_MMIO_BASE, 0x1000)
		Memory32Fixed (ReadWrite, 0xfed20000, 0x00020000) /* TXT */
		Memory32Fixed (ReadWrite, 0xfed40000, 0x00005000) /* TPM */
	})

	Method (_CRS, 0, Serialized)
	{
		Return (PDRS)
	}
}
