/* SPDX-License-Identifier: GPL-2.0-only */

#define __SIMPLE_DEVICE__

#include <console/console.h>
#include <cpu/x86/msr.h>
#include <device/pci_ops.h>
#include "grantley.h"

#define UBOX_DFX_DEV PCI_DEV(0xff, 16, 6)
#define SMM_FEATURE_CONTROL 0x58
#define SMM_FEATURE_LOCK (1 << 0)
#define SMM_CODE_CHECK (1 << 2)
#define IA32_SMM_MCA_CAP 0x17d
#define SMM_CODE_CHECK_CAP_HI (1 << (58 - 32))

/* Called from SMM by the PCH finalization handler. Server control is a CSR. */
void enable_smm_code_access_check(void)
{
	if (!(rdmsr(IA32_SMM_MCA_CAP).hi & SMM_CODE_CHECK_CAP_HI))
		return;
	uint32_t feature = pci_s_read_config32(UBOX_DFX_DEV, SMM_FEATURE_CONTROL);
	if (feature == UINT32_MAX)
		die("Grantley: SMM feature control unavailable\n");
	if (feature & SMM_FEATURE_LOCK) {
		if (!(feature & SMM_CODE_CHECK))
			printk(BIOS_WARNING, "Grantley: SMM code check locked off\n");
		return;
	}
	feature |= SMM_CODE_CHECK | SMM_FEATURE_LOCK;
	pci_s_write_config32(UBOX_DFX_DEV, SMM_FEATURE_CONTROL, feature);
	if ((pci_s_read_config32(UBOX_DFX_DEV, SMM_FEATURE_CONTROL) &
	     (SMM_CODE_CHECK | SMM_FEATURE_LOCK)) != (SMM_CODE_CHECK | SMM_FEATURE_LOCK))
		die("Grantley: failed to lock SMM code check\n");
}
