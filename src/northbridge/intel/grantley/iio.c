/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <delay.h>
#include <device/pci_ops.h>
#include "chip.h"
#include "iio.h"

#define IIO_BIFCTRL		0x190
#define  IIO_BIFCTRL_LNKCFG	0x7
#define  IIO_BIFCTRL_SBIF	(1 << 3)

/* Out of reset the IIO ports stay in Detect until bifurcation is started. */
static void iio_start_bifurcation(pci_devfn_t dev, u8 cfg)
{
	u16 ctrl = pci_s_read_config16(dev, IIO_BIFCTRL);

	ctrl &= ~IIO_BIFCTRL_LNKCFG;
	ctrl |= (cfg & IIO_BIFCTRL_LNKCFG) | IIO_BIFCTRL_SBIF;
	pci_s_write_config16(dev, IIO_BIFCTRL, ctrl);
}

void grantley_iio_start_links(const struct northbridge_intel_grantley_config *cfg)
{
	iio_start_bifurcation(PCI_DEV(0, 1, 0), cfg->iou2_bifurcation);
	iio_start_bifurcation(PCI_DEV(0, 2, 0), cfg->iou0_bifurcation);
	iio_start_bifurcation(PCI_DEV(0, 3, 0), cfg->iou1_bifurcation);

	printk(BIOS_DEBUG, "IIO: bifurcation IOU0=%u IOU1=%u IOU2=%u\n",
	       cfg->iou0_bifurcation, cfg->iou1_bifurcation, cfg->iou2_bifurcation);
	mdelay(400);
}
