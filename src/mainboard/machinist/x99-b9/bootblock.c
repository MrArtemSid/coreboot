/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/pnp_ops.h>
#include <southbridge/intel/lynxpoint/pch.h>
#include <superio/nuvoton/common/nuvoton.h>
#include <superio/nuvoton/nct5539d/nct5539d.h>

void mainboard_config_superio(void)
{
	nuvoton_enable_serial(PNP_DEV(0x2e, NCT5539D_SP1), CONFIG_TTYS0_BASE);
}
