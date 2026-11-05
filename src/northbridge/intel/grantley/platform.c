/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/pci_ids.h>
#include "platform.h"

bool grantley_pch_is_supported(uint16_t vendor, uint16_t device)
{
	if (vendor != PCI_VID_INTEL)
		return false;

	/* Wellsburg C610/C612 / X99 family */
	if (device >= 0x8d40 && device <= 0x8d5f)
		return true;

	switch (device) {
	/* Lynx Point 8-series desktop & server */
	case PCI_DID_INTEL_LPT_Z87:
	case PCI_DID_INTEL_LPT_H87:
	case PCI_DID_INTEL_LPT_Q85:
	case PCI_DID_INTEL_LPT_Q87:
	case PCI_DID_INTEL_LPT_B85:
	case PCI_DID_INTEL_LPT_C222:
	case PCI_DID_INTEL_LPT_C224:
	case PCI_DID_INTEL_LPT_C226:
	case PCI_DID_INTEL_LPT_H81:
	/* Wildcat Point 9-series */
	case PCI_DID_INTEL_LPT_Z97:
	case PCI_DID_INTEL_LPT_H97:
		return true;
	default:
		return false;
	}
}
