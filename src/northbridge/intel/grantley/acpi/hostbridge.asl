/* SPDX-License-Identifier: GPL-2.0-only */

#include <southbridge/intel/lynxpoint/pch.h>

External (\_SB.CP00, DeviceObj)

Name (_HID, EISAID ("PNP0A08"))	/* PCI Express Bus */
Name (_CID, EISAID ("PNP0A03"))	/* PCI Bus */
Name (_BBN, 0)			/* Bus Number 0 */
Name (_UID, 0)

/*
 * Intel Grantley IIO System Management / Host Bridge
 * Bus 0, Device 5, Function 0
 */
Device (IIO)
{
	Name (_ADR, 0x00050000)	/* 00:05.0 */

	OperationRegion (IIOP, PCI_Config, 0x00, 0x110)
	Field (IIOP, DWordAcc, NoLock, Preserve)
	{
		Offset (0x84),	/* MMCFG base on Haswell-EP */
		MM84, 32,
		Offset (0x90),	/* MMCFG base on Broadwell-EP */
		MM90, 32,
		Offset (0xd0),	/* TOLM (Top of Low Memory) */
		TOLM, 32,
		Offset (0xd4),	/* TOHM_0 (Top of High Memory low 32-bit) */
		TOH0, 32,
		Offset (0xd8),	/* TOHM_1 (Top of High Memory high 32-bit) */
		TOH1, 32,
		Offset (0x108),	/* CPUBUSNO (uncore routing) */
		CPUB, 32,
	}
}

/* Current Resource Settings for Root PCI Bus 0 */
Name (MCRS, ResourceTemplate ()
{
	/* Bus Numbers (0x00 .. 0x7f for Socket 0) */
	WordBusNumber (ResourceProducer, MinFixed, MaxFixed, PosDecode,
			0x0000, 0x0000, 0x007f, 0x0000, 0x0080,,, PB00)

	/* Low Legacy I/O Region 0 (0x0000 .. 0x0cf7) */
	DWordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
			0x0000, 0x0000, 0x0cf7, 0x0000, 0x0cf8,,, PI00)

	/* PCI Config Space Mechanism 1 ports */
	Io (Decode16, 0x0cf8, 0x0cf8, 0x0001, 0x0008)

	/* High I/O Region 1 (0x0d00 .. 0xffff) */
	DWordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
			0x0000, 0x0d00, 0xffff, 0x0000, 0xf300,,, PI01)

	/* VGA memory (0x000a0000 .. 0x000bffff) */
	DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
			Cacheable, ReadWrite,
			0x00000000, 0x000a0000, 0x000bffff, 0x00000000,
			0x00020000,,, ASEG)

	/* Option ROM regions (0x000c0000 .. 0x000fffff) */
	DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
			Cacheable, ReadWrite,
			0x00000000, 0x000c0000, 0x000c3fff, 0x00000000,
			0x00004000,,, OPR0)
	DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
			Cacheable, ReadWrite,
			0x00000000, 0x000c4000, 0x000c7fff, 0x00000000,
			0x00004000,,, OPR1)
	DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
			Cacheable, ReadWrite,
			0x00000000, 0x000c8000, 0x000cbfff, 0x00000000,
			0x00004000,,, OPR2)
	DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
			Cacheable, ReadWrite,
			0x00000000, 0x000cc000, 0x000cffff, 0x00000000,
			0x00004000,,, OPR3)
	DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
			Cacheable, ReadWrite,
			0x00000000, 0x000d0000, 0x000d3fff, 0x00000000,
			0x00004000,,, OPR4)
	DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
			Cacheable, ReadWrite,
			0x00000000, 0x000d4000, 0x000d7fff, 0x00000000,
			0x00004000,,, OPR5)
	DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
			Cacheable, ReadWrite,
			0x00000000, 0x000d8000, 0x000dbfff, 0x00000000,
			0x00004000,,, OPR6)
	DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
			Cacheable, ReadWrite,
			0x00000000, 0x000dc000, 0x000dffff, 0x00000000,
			0x00004000,,, OPR7)
	DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
			Cacheable, ReadWrite,
			0x00000000, 0x000e0000, 0x000e3fff, 0x00000000,
			0x00004000,,, ESG0)
	DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
			Cacheable, ReadWrite,
			0x00000000, 0x000e4000, 0x000e7fff, 0x00000000,
			0x00004000,,, ESG1)
	DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
			Cacheable, ReadWrite,
			0x00000000, 0x000e8000, 0x000ebfff, 0x00000000,
			0x00004000,,, ESG2)
	DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
			Cacheable, ReadWrite,
			0x00000000, 0x000ec000, 0x000effff, 0x00000000,
			0x00004000,,, ESG3)
	DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
			Cacheable, ReadWrite,
			0x00000000, 0x000f0000, 0x000fffff, 0x00000000,
			0x00010000,,, FSEG)

	/* 32-bit PCI MMIO Region (0x80000000 .. 0xfdffffff) */
	DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
			NonCacheable, ReadWrite,
			0x00000000, 0x80000000, 0xfdffffff, 0x00000000,
			0x7e000000,,, PM01)

	/* TPM Area (0xfed40000 .. 0xfed44fff) */
	DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
			Cacheable, ReadWrite,
			0x00000000, 0xfed40000, 0xfed44fff, 0x00000000,
			0x00005000,,, TPMR)

	/* High 64-bit PCI MMIO Region (0x3800000000 .. 0x3fffffffff, 32 GiB) */
	QWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
			NonCacheable, ReadWrite,
			0x0000000000, 0x3800000000, 0x3fffffffff, 0x0000000000,
			0x0800000000,,, PM02)
})

Method (_CRS, 0, Serialized)
{
	/*
	 * Dynamically adjust 32-bit PCI MMIO window based on IIO TOLM register
	 * if programmed, falling back to safe 0x80000000 boundary.
	 */
	CreateDwordField (MCRS, ^PM01._MIN, PMIN)
	CreateDwordField (MCRS, ^PM01._MAX, PMAX)
	CreateDwordField (MCRS, ^PM01._LEN, PLEN)

	Local0 = ^IIO.TOLM & 0xfc000000
	If ((Local0 >= 0x40000000) && (Local0 <= 0xd0000000)) {
		PMIN = Local0
		PLEN = (PMAX - PMIN) + 1
	}

	Return (MCRS)
}

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
