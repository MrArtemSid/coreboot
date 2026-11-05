/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <acpi/acpi_device.h>
#include <acpi/acpigen.h>
#include <acpi/acpigen_pci.h>
#include <device/device.h>
#include <device/pci.h>
#include <device/pci_ids.h>
#include <device/pciexp.h>
#include <stdio.h>
#include <stdlib.h>

static struct device_operations iio_root_port_ops;

static const char *iio_root_port_acpi_name(const struct device *dev)
{
	if (!is_pci(dev) || !dev->upstream)
		return NULL;

	/* Only direct children inherit this port's naming callback. */
	if (dev->upstream->dev && dev->upstream->dev->ops == &iio_root_port_ops)
		return dev->name;

	if (dev->upstream->secondary != 0)
		return NULL;

	/* Keep CPU IIO ports separate from the PCH's RP01..RP08 namespace. */
	switch (dev->path.pci.devfn) {
	case PCI_DEVFN(1, 0): return "IIO0";
	case PCI_DEVFN(1, 1): return "IIO1";
	case PCI_DEVFN(2, 0): return "IIO2";
	case PCI_DEVFN(2, 1): return "IIO3";
	case PCI_DEVFN(2, 2): return "IIO4";
	case PCI_DEVFN(2, 3): return "IIO5";
	case PCI_DEVFN(3, 0): return "IIO6";
	case PCI_DEVFN(3, 1): return "IIO7";
	case PCI_DEVFN(3, 2): return "IIO8";
	case PCI_DEVFN(3, 3): return "IIO9";
	default: return NULL;
	}
}

static void iio_root_port_init(struct device *dev)
{
	struct device *child = NULL;

	while ((child = dev_bus_each_child(dev->downstream, child))) {
		if (!is_pci(child) || !child->enabled)
			continue;

		/* As on Xeon SP, give each PCI function a distinct ACPI name. */
		char *name = xmalloc(ACPI_NAME_BUFFER_SIZE);
		snprintf(name, ACPI_NAME_BUFFER_SIZE, "DC%02X", child->path.pci.devfn);
		child->name = name;
	}
}

static void iio_root_port_fill_ssdt(const struct device *dev)
{
	const char *scope = acpi_device_scope(dev);
	const char *name = acpi_device_name(dev);

	if (!dev->enabled || !scope || !name)
		return;

	acpigen_write_scope(scope);
	acpigen_write_device(name);
	acpigen_write_ADR_pci_device(dev);

	/* Declare children before their callbacks add methods such as _ROM. */
	struct device *child = NULL;
	while ((child = dev_bus_each_child(dev->downstream, child))) {
		if (!is_pci(child) || !child->enabled)
			continue;

		name = acpi_device_name(child);
		if (!name)
			continue;

		acpigen_write_device(name);
		acpigen_write_ADR_pci_device(child);
		acpigen_write_device_end();
	}

	acpigen_write_device_end();
	acpigen_write_scope_end();
}

/* Preserve the generic PCIe bridge's lack of subsystem-ID programming. */
static struct pci_operations iio_root_port_pci_ops = {
	.set_subsystem = NULL,
};

static struct device_operations iio_root_port_ops = {
	.read_resources = pci_bus_read_resources,
	.set_resources = pci_dev_set_resources,
	.enable_resources = pci_bus_enable_resources,
	.init = iio_root_port_init,
	.scan_bus = pciexp_scan_bridge,
	.reset_bus = pci_bus_reset,
	.acpi_name = iio_root_port_acpi_name,
	.acpi_fill_ssdt = iio_root_port_fill_ssdt,
	.ops_pci = &iio_root_port_pci_ops,
};

static const unsigned short iio_root_port_ids[] = {
	/* Haswell-EP: ports 1A..1B, 2A..2D and 3A..3D. */
	0x2f02, 0x2f03, 0x2f04, 0x2f05, 0x2f06,
	0x2f07, 0x2f08, 0x2f09, 0x2f0a, 0x2f0b,
	/* Broadwell-EP: the same IIO port layout. */
	0x6f02, 0x6f03, 0x6f04, 0x6f05, 0x6f06,
	0x6f07, 0x6f08, 0x6f09, 0x6f0a, 0x6f0b,
	0
};

static const struct pci_driver iio_root_port_driver __pci_driver = {
	.ops = &iio_root_port_ops,
	.vendor = PCI_VID_INTEL,
	.devices = iio_root_port_ids,
};
