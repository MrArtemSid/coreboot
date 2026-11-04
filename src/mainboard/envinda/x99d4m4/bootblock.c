/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/pnp_ops.h>
#include <southbridge/intel/lynxpoint/pch.h>
#include <superio/nuvoton/common/nuvoton.h>
#include <superio/nuvoton/nct5539d/nct5539d.h>

/*
 * One generic call, exactly as QD4 and BD4 do it: LDN 2 (UART A) at
 * CONFIG_TTYS0_BASE.
 *
 * The 55-record stream of the stock AMI NCT5532DPeiInit PEIM (D4M4-orig.bin,
 * table at 0xffdcd6ac) is no longer replayed here, so its other effects are
 * not written before ramstage any more: the multi-function pin mux
 * (CR1a/1b/1d/2a/2c/2f/3e), the KBC setup on LDN 5 (0x60/0x64, IRQ1,
 * CRf0 = 0x83) and disabling the CIR on LDN 6.  LDN 5 is programmed by the
 * nct5539d ramstage driver this board selects; the mux registers and LDN 6
 * keep their reset values until something writes them.
 *
 * CONFIG_SUPERIO_NUVOTON_COMMON_COM_A is not selected for this board, so the
 * helper does not touch CR2A either: it keeps the chip default 0xc0, whose
 * bit 7 routes COM A to the GPIO8 pin group, while stock leaves CR2A at 0x08.
 * UART A output has never been observed on a D4M4, so the console pin group
 * is an assumption and this path must not be described as working until a
 * hardware capture shows it.
 */
void mainboard_config_superio(void)
{
	nuvoton_enable_serial(PNP_DEV(0x2e, NCT5539D_SP1), CONFIG_TTYS0_BASE);
}
