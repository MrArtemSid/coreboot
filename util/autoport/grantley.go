package main

type grantleymc struct {
	variant string
}

func (g grantleymc) Scan(ctx Context, addr PCIDevData) {
	DevTree = DevTreeNode{
		Chip:          "northbridge/intel/grantley",
		MissingParent: "northbridge",
		Comment:       "Intel Haswell-EP / Broadwell-EP (Grantley LGA2011-3)",
		Children: []DevTreeNode{
			{
				Chip: "cpu/intel/grantley",
				Children: []DevTreeNode{
					{
						Chip: "cpu_cluster",
						Dev:  0,
						Ops:  "grantley_cpu_bus_ops",
					},
				},
			},

			{
				Chip:          "domain",
				Dev:           0,
				Ops:           "grantley_pci_domain_ops",
				PCIController: true,
				ChildPCIBus:   0,
				PCISlots: []PCISlot{
					PCISlot{PCIAddr: PCIAddr{Dev: 0x0, Func: 0}, writeEmpty: true, additionalComment: "Server Host bridge"},
					PCISlot{PCIAddr: PCIAddr{Dev: 0x1, Func: 0}, writeEmpty: true, additionalComment: "PCIe Root Port 1 (x16 slot)"},
					PCISlot{PCIAddr: PCIAddr{Dev: 0x2, Func: 0}, writeEmpty: true, additionalComment: "PCIe Root Port 2 (x16 slot)"},
					PCISlot{PCIAddr: PCIAddr{Dev: 0x3, Func: 0}, writeEmpty: true, additionalComment: "PCIe Root Port 3 (x8 slot)"},
					PCISlot{PCIAddr: PCIAddr{Dev: 0x5, Func: 0}, writeEmpty: true, additionalComment: "Address Map / IIO Misc"},
					PCISlot{PCIAddr: PCIAddr{Dev: 0x5, Func: 1}, writeEmpty: true, additionalComment: "IIO Hot Plug"},
					PCISlot{PCIAddr: PCIAddr{Dev: 0x5, Func: 2}, writeEmpty: true, additionalComment: "IIO RAS, Error Status"},
					PCISlot{PCIAddr: PCIAddr{Dev: 0x5, Func: 4}, writeEmpty: true, additionalComment: "IIO I/O APIC"},
				},
			},
		},
	}

	PutPCIDev(addr, "Host bridge")

	KconfigSelect["NORTHBRIDGE_INTEL_GRANTLEY"] = ""
	KconfigSelect["HAVE_ACPI_TABLES"] = ""
	KconfigSelect["HAVE_ACPI_RESUME"] = ""

	DSDTIncludes = append(DSDTIncludes, DSDTInclude{
		File: "cpu/intel/common/acpi/cpu.asl",
	})
}

func init() {
	/* Haswell-EP (0x2f00) and Broadwell-EP (0x6f00) Host Bridges */
	RegisterPCI(0x8086, 0x2f00, grantleymc{variant: "Haswell-EP"})
	RegisterPCI(0x8086, 0x6f00, grantleymc{variant: "Broadwell-EP"})

	/* Haswell-EP PCIe Root Ports 1-3 */
	for _, id := range []uint16{
		0x2f02, 0x2f03, 0x2f04, 0x2f05, 0x2f06, 0x2f07, 0x2f08, 0x2f09, 0x2f0a, 0x2f0b,
	} {
		RegisterPCI(0x8086, id, GenericPCI{})
	}

	/* Broadwell-EP PCIe Root Ports 1-3 */
	for _, id := range []uint16{
		0x6f02, 0x6f03, 0x6f04, 0x6f05, 0x6f06, 0x6f07, 0x6f08, 0x6f09, 0x6f0a, 0x6f0b,
	} {
		RegisterPCI(0x8086, id, GenericPCI{})
	}

	/* IIO System Peripherals (Bus 0, Dev 5) */
	for _, id := range []uint16{
		0x2f28, 0x2f29, 0x2f2a, 0x2f2c, /* Haswell-EP Dev 5 */
		0x6f28, 0x6f29, 0x6f2a, 0x6f2c, /* Broadwell-EP Dev 5 */
	} {
		RegisterPCI(0x8086, id, GenericPCI{})
	}

	/* Uncore internal Xeon devices (ignore as uncore/removable) */
	for id := uint16(0x2f1d); id <= 0x2fff; id++ {
		RegisterPCI(0x8086, id, GenericPCI{})
	}
	for id := uint16(0x6f1d); id <= 0x6fff; id++ {
		RegisterPCI(0x8086, id, GenericPCI{})
	}
}
