/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/pnp_ops.h>
#include <southbridge/intel/lynxpoint/pch.h>
#include <superio/nuvoton/common/nuvoton.h>
#include <superio/nuvoton/nct6779d/nct6779d.h>

#define GLOBAL_DEV	PNP_DEV(0x2e, 0)
#define SERIAL_DEV	PNP_DEV(0x2e, NCT6779D_SP1)

void mainboard_config_superio(void)
{
	nuvoton_pnp_enter_conf_state(GLOBAL_DEV);

	/* Select SIO pin mux states */
	pnp_write_config(GLOBAL_DEV, 0x1a, 0x00);
	pnp_write_config(GLOBAL_DEV, 0x1b, 0x70);
	pnp_write_config(GLOBAL_DEV, 0x1c, 0xf7);
	pnp_write_config(GLOBAL_DEV, 0x1d, 0x00);
	pnp_write_config(GLOBAL_DEV, 0x24, 0x00);
	pnp_write_config(GLOBAL_DEV, 0x25, 0x00);
	pnp_write_config(GLOBAL_DEV, 0x26, 0x10);
	pnp_write_config(GLOBAL_DEV, 0x27, 0x00);
	pnp_write_config(GLOBAL_DEV, 0x28, 0x00);
	pnp_write_config(GLOBAL_DEV, 0x2a, 0x00);
	pnp_write_config(GLOBAL_DEV, 0x2b, 0x00);
	pnp_write_config(GLOBAL_DEV, 0x2c, 0x00);
	pnp_write_config(GLOBAL_DEV, 0x2f, 0x01);

	nuvoton_pnp_exit_conf_state(GLOBAL_DEV);

	nuvoton_enable_serial(SERIAL_DEV, CONFIG_TTYS0_BASE);
}
