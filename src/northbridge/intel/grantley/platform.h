/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NORTHBRIDGE_INTEL_GRANTLEY_PLATFORM_H
#define NORTHBRIDGE_INTEL_GRANTLEY_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>
#include <device/pci_ids.h>

struct grantley_board_identity {
	uint8_t board_id;
	uint32_t policy_revision;
};

/* Return false to preserve the default PlatformInfo policy. */
bool mainboard_get_board_identity(struct grantley_board_identity *identity);

/* Supported PCH device IDs on Grantley hybrid / native platforms,
 * aliased to the shared pci_ids.h definitions. */
enum grantley_pch_device {
	GRANTLEY_PCH_Z87         = PCI_DID_INTEL_LPT_Z87,
	GRANTLEY_PCH_H87         = PCI_DID_INTEL_LPT_H87,
	GRANTLEY_PCH_Q85         = PCI_DID_INTEL_LPT_Q85,
	GRANTLEY_PCH_Q87         = PCI_DID_INTEL_LPT_Q87,
	GRANTLEY_PCH_B85         = PCI_DID_INTEL_LPT_B85,
	GRANTLEY_PCH_C222        = PCI_DID_INTEL_LPT_C222,
	GRANTLEY_PCH_C224        = PCI_DID_INTEL_LPT_C224,
	GRANTLEY_PCH_C226        = PCI_DID_INTEL_LPT_C226,
	GRANTLEY_PCH_H81         = PCI_DID_INTEL_LPT_H81,
	GRANTLEY_PCH_Z97         = PCI_DID_INTEL_LPT_Z97,
	GRANTLEY_PCH_H97         = PCI_DID_INTEL_LPT_H97,
	GRANTLEY_PCH_WBG_MIN     = 0x8d40,
	GRANTLEY_PCH_WBG_MAX     = 0x8d5f,
	GRANTLEY_PCH_C610        = PCI_DID_INTEL_WBG_LPC_C610,
	GRANTLEY_PCH_C612        = PCI_DID_INTEL_WBG_LPC_C612,
	GRANTLEY_PCH_X99_HEDT    = PCI_DID_INTEL_WBG_LPC_HEDT,
	GRANTLEY_PCH_X99         = PCI_DID_INTEL_WBG_LPC_X99,
	GRANTLEY_PCH_X99_ALT     = PCI_DID_INTEL_WBG_LPC_X99_ALT,
};

/* PCH compatibility check for Grantley platform (Lynx Point, Wildcat Point, Wellsburg). */
bool grantley_pch_is_supported(uint16_t vendor, uint16_t device);

#endif
