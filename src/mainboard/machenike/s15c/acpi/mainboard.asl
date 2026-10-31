/* SPDX-License-Identifier: GPL-2.0-only */

/* Minimal mainboard ACPI for the Machenike S15C (V155PNPQ-HMx).
 *
 * The stock ITE IT5570E EC firmware stays in place and handles fans,
 * battery and keyboard autonomously. TODO: add an EC device (PNP0C09)
 * and battery/thermal methods once the EC interface is reverse
 * engineered; for now the generic ACPI EC is exposed by the SoC code.
 */
