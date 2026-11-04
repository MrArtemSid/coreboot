/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/pnp_def.h>
#include <southbridge/intel/lynxpoint/pch.h>
#include <superio/nuvoton/common/nuvoton.h>

/*
 * Super I/O programming for the Angxun X99-XD4.
 *
 * The board's stock Super I/O PEIM is NCT5532DPeiInit (UEFIExtract volume
 * 61C0F511-A691-4F54-974F-B9A42172CE53, file 49, GUID
 * 9029F23E-E1EE-40D1-9382-36DD61A63EAA) in X9XD4006.bin (X99-XD4
 * V2.12_006_2026-6-5, sha256
 * b11d2aa9d3f4c230581610279e6d5db0b5e57c94127281b9ffc7daf51d3ca053).
 * Its 61-record index/data stream is not replayed here any more.
 *
 * What that stream did and is therefore no longer done before ramstage: the
 * multi-function pin mux (CR2f/1a/1b/1c/1d/2a/2c), the KBC on LDN 5
 * (0x60/0x64, IRQ1 + IRQ12, CRf0 = 0x83), the LDN 0x0a register pair
 * ((CRe4 & 0x90) | 0x20, CRe7 & 0xef) and disabling LDN 6.  ramstage then
 * programs the LDNs the devicetree declares, through the NCT5539D driver
 * selected as a register-compatibility mapping: only LDN 5 is on, so the KBC
 * resources are programmed there and every other declared LDN has its CR30
 * enable bit cleared.
 *
 * UART A is not established either: the stock PEIM never selects LDN 2, so
 * its logical device number and base stay unproven.  The generic helper is
 * reachable only through CONFIG_XD4_ASSUMED_UART_A_LDN2, a legacy assumption
 * that defaults to n; with it off this function issues no port access at all,
 * and no console output has been observed on hardware.  LDN 2 is declared off
 * in devicetree.cb because sconfig cannot make a node conditional, so the
 * ramstage scan clears its CR30 bit even when the option is set.
 */
void mainboard_config_superio(void)
{
	if (CONFIG(XD4_ASSUMED_UART_A_LDN2))
		nuvoton_enable_serial(PNP_DEV(0x2e, 2), CONFIG_TTYS0_BASE);
}
