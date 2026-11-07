/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/helpers.h>
#include <console/console.h>
#include <delay.h>
#include <device/pci_ops.h>
#include <device/pciexp.h>
#include "chip.h"
#include "iio.h"

/* Fields of the port link capability that the bifurcation decides. */
#define IIO_LNKCAP_PORT		(0xffU << 24)
#define IIO_LNKCAP_WIDTH	(0x3fU << 4)

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

/*
 * The reset value of the link capability has no port number and the smallest
 * width, so every root port would describe itself as port 0 running x4 no
 * matter how it is wired. The OS then takes a wider link for an overdriven
 * one and sizes bandwidth from the wrong ceiling. Stock firmware fills the
 * fields in from the bifurcation while the ports are still idle, and the
 * register is write-once until reset, so it has to happen before the links
 * leave Detect.
 */
static void iio_set_link_caps(unsigned int stack, const u8 *widths,
			      unsigned int ports, unsigned int first_port)
{
	for (unsigned int i = 0; i < ports; i++) {
		const pci_devfn_t dev = PCI_DEV(0, stack, i);
		u32 lnkcap;
		u16 cap;

		if (!widths[i])
			continue;

		cap = pci_s_find_capability(dev, PCI_CAP_ID_PCIE);
		if (!cap)
			continue;

		lnkcap = pci_s_read_config32(dev, cap + PCI_EXP_LNKCAP);
		lnkcap &= ~(IIO_LNKCAP_PORT | IIO_LNKCAP_WIDTH);
		lnkcap |= ((first_port + i) << 24) | (widths[i] << 4);
		pci_s_write_config32(dev, cap + PCI_EXP_LNKCAP, lnkcap);

		printk(BIOS_DEBUG, "IIO: %02x.%x port %u, %u lanes, lnkcap %08x\n",
		       stack, i, first_port + i, widths[i],
		       pci_s_read_config32(dev, cap + PCI_EXP_LNKCAP));
	}
}

void grantley_iio_init(const struct northbridge_intel_grantley_config *cfg)
{
	if (!cfg->start_iio_links)
		return;

	/*
	 * Widths each function of a stack ends up with, indexed by the
	 * bifurcation mode. A zero marks a function the mode disables.
	 *
	 * Functions follow the lane groups, not the ports: fn0 carries lanes
	 * 0-3, fn1 lanes 4-7, fn2 lanes 8-11 and fn3 lanes 12-15. A port that
	 * spans eight lanes therefore lands on fn0 and fn2, and fn1 stays
	 * unused, which is also how the board numbers its ports.
	 */
	static const u8 iou_x16[IIO_BIFURCATE_X16 + 1][4] = {
		[IIO_BIFURCATE_X4X4X4X4] = { 4, 4, 4, 4 },
		[IIO_BIFURCATE_X4X4X8] = { 4, 4, 8, 0 },
		[IIO_BIFURCATE_X8X4X4] = { 8, 0, 4, 4 },
		[IIO_BIFURCATE_X8X8] = { 8, 0, 8, 0 },
		[IIO_BIFURCATE_X16] = { 16, 0, 0, 0 },
	};
	static const u8 iou_x8[][2] = {
		[IIO_BIFURCATE_X4X4] = { 4, 4 },
		[IIO_BIFURCATE_X8] = { 8, 0 },
	};

	if (cfg->iou2_bifurcation < ARRAY_SIZE(iou_x8))
		iio_set_link_caps(1, iou_x8[cfg->iou2_bifurcation],
				  ARRAY_SIZE(iou_x8[0]), 1);
	if (cfg->iou0_bifurcation < ARRAY_SIZE(iou_x16))
		iio_set_link_caps(2, iou_x16[cfg->iou0_bifurcation],
				  ARRAY_SIZE(iou_x16[0]), 3);
	if (cfg->iou1_bifurcation < ARRAY_SIZE(iou_x16))
		iio_set_link_caps(3, iou_x16[cfg->iou1_bifurcation],
				  ARRAY_SIZE(iou_x16[0]), 7);

	iio_start_bifurcation(PCI_DEV(0, 1, 0), cfg->iou2_bifurcation);
	iio_start_bifurcation(PCI_DEV(0, 2, 0), cfg->iou0_bifurcation);
	iio_start_bifurcation(PCI_DEV(0, 3, 0), cfg->iou1_bifurcation);

	printk(BIOS_DEBUG, "IIO: bifurcation IOU0=%u IOU1=%u IOU2=%u\n",
	       cfg->iou0_bifurcation, cfg->iou1_bifurcation, cfg->iou2_bifurcation);
	mdelay(400);
}

