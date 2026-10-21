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
	const uint16_t expected = CONFIG(BOARD_HUANANZHI_X99_QD4) ? 0xd121 : 0xc562;
	uint16_t id;

	nuvoton_pnp_enter_conf_state(uart);
	id = pnp_read_config(uart, 0x20) << 8;
	id |= pnp_read_config(uart, 0x21);
	nuvoton_pnp_exit_conf_state(uart);

	/* Exact capture IDs; do not guess another board revision's pin routing. */
	if (id != expected) {
		post_code(0xe1);
		halt();
	}
	nuvoton_enable_serial(uart, CONFIG_TTYS0_BASE);
}
