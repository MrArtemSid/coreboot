/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>

/*
 * Generic Grantley + Lynx Point desktop DSDT.  No board-specific device is
 * described here: the X99-XD4 thermal and fan ACPI surfaces have not been
 * recovered.  The Super I/O LDNs are emitted by the driver's own SSDT
 * generator for the nodes the devicetree declares (only LDN 5 is enabled),
 * so nothing is added for it here.
 */
DefinitionBlock(
	"dsdt.aml",
	"DSDT",
	ACPI_DSDT_REV_2,
	OEM_ID,
	ACPI_TABLE_CREATOR,
	0x20141018	/* OEM revision */
)
{
	#include <acpi/dsdt_top.asl>
	#include <southbridge/intel/common/acpi/platform.asl>
	#include <southbridge/intel/lynxpoint/acpi/globalnvs.asl>
	#include <southbridge/intel/common/acpi/sleepstates.asl>
	#include <cpu/intel/common/acpi/cpu.asl>

	Device (\_SB.PCI0)
	{
		#include <northbridge/intel/grantley/acpi/hostbridge.asl>
		#include <southbridge/intel/lynxpoint/acpi/pch.asl>
	}
}
