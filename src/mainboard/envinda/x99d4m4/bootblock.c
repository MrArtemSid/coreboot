/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/pnp_ops.h>
#include <halt.h>
#include <post.h>
#include <stdint.h>
#include <southbridge/intel/lynxpoint/pch.h>
#include <superio/nuvoton/common/nuvoton.h>

void mainboard_config_superio(void)
{
	const pnp_devfn_t uart = PNP_DEV(0x2e, 2);
	uint16_t id;

	nuvoton_pnp_enter_conf_state(uart);
	id = pnp_read_config(uart, 0x20) << 8;
	id |= pnp_read_config(uart, 0x21);
	nuvoton_pnp_exit_conf_state(uart);

	/* NCT5532D / NCT5539D chip ID is 0xd121 */
	if (id != 0xd121) {
		post_code(0xee);
		halt();
	}

	nuvoton_enable_serial(uart, CONFIG_TTYS0_BASE);
}
