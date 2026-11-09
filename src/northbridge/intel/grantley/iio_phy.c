/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/cpu.h>
#include <commonlib/helpers.h>
#include <console/console.h>
#include <cpu/intel/grantley/grantley_routing.h>
#include <delay.h>
#include <device/pci_ops.h>
#include "iio_phy.h"
#include "iio_phy_recipe.h"

#define CPUID_HASWELL_EP	0x306f0

#define IIO_DFX_PORT_GLOBAL	15

/* PCU function 3, CAPID0 bit 30: the part is fused for DCU mode. */
#define PCU_CAPID0		0x84
#define PCU_CAPID0_DCU_MODE	(1 << 30)

/* Settle time the receivers need after being reprogrammed. */
#define IIO_PHY_SETTLE_US	180

/*
 * Ports 1-6 sit on DFX functions 06.1-06.6 and ports 7-10 on 07.0-07.3,
 * port 0 is the DMI link on 06.0 and the global settings live on 06.7.
 */
static pci_devfn_t iio_dfx_dev(u8 port)
{
	if (port == IIO_DFX_PORT_GLOBAL)
		return PCI_DEV(0, 6, 7);
	if (port < 7)
		return PCI_DEV(0, 6, port);
	return PCI_DEV(0, 7, port - 7);
}

static unsigned int iio_phy_apply(const char *name, const struct iio_phy_setting *table,
				  size_t count)
{
	unsigned int mismatches = 0;

	for (size_t i = 0; i < count; i++) {
		const struct iio_phy_setting *s = &table[i];
		const pci_devfn_t dev = iio_dfx_dev(s->port);
		u32 readback;

		switch (s->size) {
		case 1:
			pci_s_write_config8(dev, s->reg,
				(pci_s_read_config8(dev, s->reg) & s->keep) | s->value);
			readback = pci_s_read_config8(dev, s->reg);
			break;
		case 2:
			pci_s_write_config16(dev, s->reg,
				(pci_s_read_config16(dev, s->reg) & s->keep) | s->value);
			readback = pci_s_read_config16(dev, s->reg);
			break;
		default:
			pci_s_write_config32(dev, s->reg,
				(pci_s_read_config32(dev, s->reg) & s->keep) | s->value);
			readback = pci_s_read_config32(dev, s->reg);
			break;
		}

		const u32 mask = s->size == 4 ? ~s->keep : ~s->keep & ((1U << (8 * s->size)) - 1);
		if ((readback & mask) != (s->value & mask)) {
			mismatches++;
			printk(BIOS_SPEW, "IIO PHY: %s port %u reg %03x reads %08x\n",
			       name, s->port, s->reg, readback);
		}
	}

	printk(BIOS_DEBUG, "IIO PHY: %s, %zu settings, %u did not stick\n",
	       name, count, mismatches);
	return mismatches;
}

/*
 * Program the receivers of the IIO PCIe PHYs before the links are started.
 * Out of reset the receiver equalization, reference voltages and timers are
 * not tuned for the platform, and links come up late, at a reduced width or
 * not at all. The settings are the ones the vendor firmware applies: a
 * common set, a set for the CPU stepping and, from B0 on, a CTLE update
 * that does not apply to parts fused for DCU mode.
 */
void grantley_iio_phy_init(void)
{
	const u32 signature = cpuid_eax(1);
	const u8 stepping = signature & 0xf;

	if ((signature & ~0xf) != CPUID_HASWELL_EP)
		return;

	iio_phy_apply("common", hsx_phy_common, ARRAY_SIZE(hsx_phy_common));

	switch (stepping) {
	case 0:
		iio_phy_apply("A0", hsx_phy_a0, ARRAY_SIZE(hsx_phy_a0));
		break;
	case 1:
		iio_phy_apply("B0", hsx_phy_b0, ARRAY_SIZE(hsx_phy_b0));
		break;
	case 2:
		iio_phy_apply("C0", hsx_phy_c0, ARRAY_SIZE(hsx_phy_c0));
		break;
	default:
		printk(BIOS_WARNING, "IIO PHY: no settings for stepping %u\n", stepping);
		break;
	}

	const u32 capid0 = pci_s_read_config32(PCI_DEV(grantley_uncore_bus(), 30, 3),
					       PCU_CAPID0);
	if (stepping >= 1 && !(capid0 & PCU_CAPID0_DCU_MODE))
		iio_phy_apply("CTLE", hsx_phy_ctle, ARRAY_SIZE(hsx_phy_ctle));

	udelay(IIO_PHY_SETTLE_US);
}
