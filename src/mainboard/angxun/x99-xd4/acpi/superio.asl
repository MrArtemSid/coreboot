/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * No board-specific Super I/O ACPI device is described here.  The X99-XD4 part
 * is a NCT5532D (established from the board's own stock NCT5532DPeiInit PEIM);
 * the NCT5539D driver is selected in Kconfig as a register-compatibility
 * mapping for the LDNs the stock stream selects, and that driver's own SSDT
 * generator emits the devices for the devicetree nodes.  Only LDN 5 (KBC) is
 * enabled there and every other node is off, so no UART, HWM or thermal device
 * is claimed.  This file exists for the LPC ACPI scope and stays empty.
 */
