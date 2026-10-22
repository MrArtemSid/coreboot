/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/pci_ops.h>
#include <device/pnp_ops.h>
#include <stdint.h>
#include <southbridge/intel/lynxpoint/pch.h>
#include <superio/nuvoton/common/nuvoton.h>

void mainboard_config_superio(void)
{
	const pnp_devfn_t uart = PNP_DEV(0x2e, 2);

	/* Ensure LPC decode for 0x0a00..0x0a3f is open early */
	pci_write_config32(PCH_LPC_DEV, 0x84, 0x003c0a01);

	nuvoton_enable_serial(uart, CONFIG_TTYS0_BASE);
}
