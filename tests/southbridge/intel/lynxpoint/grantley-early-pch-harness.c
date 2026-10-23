/* SPDX-License-Identifier: GPL-2.0-only */
#include "grantley-early-pch-shim.h"
#include "../../../../src/southbridge/intel/lynxpoint/early_pch.c"
#if CONFIG_NORTHBRIDGE_INTEL_GRANTLEY && ENV_ROMSTAGE
#include "../../../../src/southbridge/intel/lynxpoint/early_pch_grantley.c"
#endif

static void exercise(u16 vendor, u16 device, bool failed, u8 index, u32 gcs)
{
	memset(pci, 0x5a, sizeof(pci));
	memset(rtc, 0xa5, sizeof(rtc));
	memset(rcba_space, 0, sizeof(rcba_space));
	rtc_write_count = io_count = mmio_writes = 0;
	at_smbus = false;
	pci_write_config16(0, PCI_VENDOR_ID, vendor);
	pci_write_config16(0, PCI_DEVICE_ID, device);
	pci[GEN_PMCON_3] = (failed ? RTC_BATTERY_DEAD : 0) | 2;
	rtc_index = index;
	rtc[RTC_REG_A] = 0x0b;
	rtc[RTC_REG_B] = 0x16;
	/* The recovery scope mirrors early_pch_grantley.c: every PCH family
	 * that can sit under Grantley, not only Wellsburg SKUs. */
	const bool recovery_pch = CONFIG_NORTHBRIDGE_INTEL_GRANTLEY && ENV_ROMSTAGE &&
		vendor == PCI_VID_INTEL &&
		((device >= 0x8c40 && device <= 0x8c5f) ||
		 (device >= 0x8cc0 && device <= 0x8ccf) ||
		 (device >= 0x8d40 && device <= 0x8d5f));
	const bool recover = recovery_pch && failed;
	expected_rtc_a = recover ? 0x2b : 0x0b;
	expected_rtc_b = 0x16;
	RCBA32(GCS) = gcs;
	RCBA32(PRSTS) = 0xffffffff;
	expected_prsts = recovery_pch ? 0xffffffef : 0xffffffff;
	u8 saved_pci[256];
	memcpy(saved_pci, pci, sizeof(pci));
	early_pch_init();
	assert(at_smbus);
	assert(rtc_write_count == (recover ? 4 : 0));
	assert(io_count == (recover ? 14 : 0));
	assert(mmio_writes == (recovery_pch ? (recover ? 3 : 1) : 0));
	if (recover) {
		assert(rtc_registers[0] == RTC_REG_A && rtc_writes[0] == 0x6b);
		assert(rtc_registers[1] == RTC_REG_B && rtc_writes[1] == 0x96);
		assert(rtc_registers[2] == RTC_REG_A && rtc_writes[2] == 0x2b);
		assert(rtc_registers[3] == RTC_REG_B && rtc_writes[3] == 0x16);
	}
	assert(rtc_index == index);
	assert(RCBA32(GCS) == (gcs | BIT(5))); /* Existing watchdog setup only. */
	/* Leave power-loss/reset evidence for the later CMOS/event handling. */
	assert(memcmp(pci + GEN_PMCON_3, saved_pci + GEN_PMCON_3, 4) == 0);
	for (unsigned int i = 0; i < sizeof(rtc); i++) {
		if (i != RTC_REG_A && i != RTC_REG_B)
			assert(rtc[i] == 0xa5);
	}
}

int main(void)
{
	const u16 devices[] = { PCI_DID_INTEL_WBG_LPC_C612, PCI_DID_INTEL_WBG_LPC_C610,
		PCI_DID_INTEL_WBG_LPC_X99, PCI_DID_INTEL_WBG_LPC_X99_ALT,
		0x8d41, 0x8d47, /* Wellsburg production / HEDT -X */
		0x8c4e, 0x8c50, 0x8c5c, /* Q87, B85, H81 (recycled client PCHs) */
		0x8cc4, /* Z97 (Wildcat Point-H) */
		0x9c41, 0x8c60, 0xffff }; /* LPT-LP and out-of-family negatives */
	for (unsigned int d = 0; d < sizeof(devices) / sizeof(devices[0]); d++)
		for (unsigned int failure = 0; failure < 2; failure++)
			for (unsigned int nmi = 0; nmi < 2; nmi++)
				for (unsigned int alt = 0; alt < 2; alt++)
					exercise(PCI_VID_INTEL, devices[d], failure,
						(nmi ? 0x80 : 0) | 0x15, 0x204 | (alt ? BIT(4) : 0));
	exercise(0xffff, PCI_DID_INTEL_WBG_LPC_C612, true, 0x85, 0x204);
	puts("RTC recovery/normal boot, NMI/index, W1C evidence and chipset scope: PASS");
	return 0;
}
