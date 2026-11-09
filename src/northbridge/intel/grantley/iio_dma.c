/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <device/device.h>
#include <device/mmio.h>
#include <device/pci.h>
#include <device/pci_def.h>
#include <device/pci_ids.h>
#include <device/pci_ops.h>
#include <types.h>

/*
 * The IIO carries up to eight DMA channels (Intel QuickData). Each channel
 * has a Direct Cache Access block inside its MMIO window: the window holds
 * the offset of the block at 0x14, and the block starts with a version
 * register, followed by the APIC ID to DCA tag map at 0x10 and 0x14. On
 * Haswell-EP the block reports version 1.0.
 *
 * Each tag map byte feeds one bit of the DCA tag: bit 7 marks the entry as
 * valid and bits 6:0 name the APIC ID bit placed there. The reset value of
 * the first five entries sets the valid bit without selecting any APIC ID
 * bit, which leaves only two possible tags, so firmware has to fill the map
 * in before the tag means anything.
 */
#define IIO_DMA_DCA_OFFSET		0x14
#define IIO_DCA_VERSION			0x00
#define  IIO_DCA_VERSION_MAJOR		0xf0
#define  IIO_DCA_VERSION_1		0x10
#define IIO_DCA_APICID_TAG_LOW		0x10
#define IIO_DCA_APICID_TAG_HIGH		0x14

/*
 * Tag bits 4:0 take APIC ID bits 4:0, so the tag follows the requester.
 * Bytes are 0x80, 0x81, 0x82, 0x83 in the low word and 0x84, 0, 0, 0 in
 * the high one; the remaining entries are unused and stay clear.
 */
#define IIO_DCA_TAG_MAP_LOW		0x83828180
#define IIO_DCA_TAG_MAP_HIGH		0x00000084

/* The DMA channel window is the first BAR, resource index 0x10. */
static uintptr_t iio_dma_bar(const struct device *dev)
{
	const struct resource *res;

	for (res = dev->resource_list; res; res = res->next) {
		if (res->index == PCI_BASE_ADDRESS_0)
			return res->base;
	}
	return 0;
}

static void iio_dma_init(struct device *dev)
{
	const uintptr_t bar = iio_dma_bar(dev);
	uintptr_t dca;
	u16 offset;
	u8 version;

	if (!bar) {
		printk(BIOS_DEBUG, "%s: no MMIO window\n", dev_path(dev));
		return;
	}

	offset = read16((void *)(bar + IIO_DMA_DCA_OFFSET));
	if (!offset) {
		printk(BIOS_DEBUG, "%s: no DCA block\n", dev_path(dev));
		return;
	}

	dca = bar + offset;
	version = read8((void *)(dca + IIO_DCA_VERSION));
	if ((version & IIO_DCA_VERSION_MAJOR) != IIO_DCA_VERSION_1) {
		printk(BIOS_WARNING, "%s: unexpected DCA version %#x\n",
		       dev_path(dev), version);
		return;
	}

	write32((void *)(dca + IIO_DCA_APICID_TAG_LOW), IIO_DCA_TAG_MAP_LOW);
	write32((void *)(dca + IIO_DCA_APICID_TAG_HIGH), IIO_DCA_TAG_MAP_HIGH);

	printk(BIOS_DEBUG, "%s: DCA tag map %08x %08x\n", dev_path(dev),
	       read32((void *)(dca + IIO_DCA_APICID_TAG_LOW)),
	       read32((void *)(dca + IIO_DCA_APICID_TAG_HIGH)));
}

static struct device_operations iio_dma_ops = {
	.read_resources		= pci_dev_read_resources,
	.set_resources		= pci_dev_set_resources,
	.enable_resources	= pci_dev_enable_resources,
	.init			= iio_dma_init,
	.ops_pci		= &pci_dev_ops_pci,
};

static const unsigned short iio_dma_ids[] = {
	0x2f20, 0x2f21, 0x2f22, 0x2f23,	/* Haswell-EP */
	0x2f24, 0x2f25, 0x2f26, 0x2f27,
	0x6f20, 0x6f21, 0x6f22, 0x6f23,	/* Broadwell-EP */
	0x6f24, 0x6f25, 0x6f26, 0x6f27,
	0
};

static const struct pci_driver iio_dma_driver __pci_driver = {
	.ops		= &iio_dma_ops,
	.vendor		= PCI_VID_INTEL,
	.devices	= iio_dma_ids,
};
