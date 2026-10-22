/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/helpers.h>
#include <console/console.h>
#include <device/device.h>
#include <device/pci.h>
#include <device/pci_ops.h>
#include <cpu/cpu.h>
#include <cpu/intel/microcode.h>
#include <cpu/x86/msr.h>
#include <cpu/x86/smm.h>
#include <types.h>
#include "platform.h"

#define MSR_OC_MAILBOX			0x150
#define  OC_MB_COMMAND_EXEC		(1ULL << 63)
#define  OC_MB_SET_FVIDS_RATIOS		(0x11ULL << 32)
#define  OC_MB_DOMAIN_IACORE		(0x00ULL << 40)
#define  OC_MB_DOMAIN_CLR		(0x02ULL << 40) /* Cache/Uncore */

static uint32_t fivr_mv_to_offset_raw(int16_t offset_mv)
{
	/* FIVR offset is 11-bit signed fixed point in units of 1/1024 V */
	int32_t val = ((int32_t)offset_mv * 1024) / 1000;
	return ((uint32_t)val & 0x7ff) << 21;
}

static void oc_mailbox_write(uint64_t cmd)
{
	msr_t msr;
	msr.lo = (uint32_t)(cmd & 0xffffffffULL);
	msr.hi = (uint32_t)(cmd >> 32);
	wrmsr(MSR_OC_MAILBOX, msr);
}

void grantley_apply_cpu_overrides(const struct grantley_cpu_params *params)
{
	if (!params)
		return;

	if (params->core_voltage_offset_mv != 0) {
		uint32_t raw = fivr_mv_to_offset_raw(params->core_voltage_offset_mv);
		uint64_t cmd = OC_MB_COMMAND_EXEC | OC_MB_SET_FVIDS_RATIOS |
			       OC_MB_DOMAIN_IACORE | (uint64_t)raw;
		printk(BIOS_INFO, "Grantley: Setting Core voltage offset %d mV (raw 0x%08x)\n",
		       params->core_voltage_offset_mv, raw);
		oc_mailbox_write(cmd);
	}

	if (params->uncore_voltage_offset_mv != 0) {
		uint32_t raw = fivr_mv_to_offset_raw(params->uncore_voltage_offset_mv);
		uint64_t cmd = OC_MB_COMMAND_EXEC | OC_MB_SET_FVIDS_RATIOS |
			       OC_MB_DOMAIN_CLR | (uint64_t)raw;
		printk(BIOS_INFO, "Grantley: Setting Uncore voltage offset %d mV (raw 0x%08x)\n",
		       params->uncore_voltage_offset_mv, raw);
		oc_mailbox_write(cmd);
	}
}

/*
 * Hook called by coreboot microcode update subsystem in ramstage.
 * If Haswell-EP turbo unlock is requested, skip updating microcode so
 * the CPU retains the unpatched state required for all-core turbo boost.
 */
int soc_skip_ucode_update(u32 current_patch_id, u32 new_patch_id)
{
	struct grantley_cpu_params params;
	mainboard_cpu_init_params(&params);

	if (params.enable_turbo_unlock) {
		uint32_t sig = cpuid_eax(1);
		if (grantley_cpu_model(sig) == GRANTLEY_HASWELL_EP) {
			printk(BIOS_INFO, "Grantley: Haswell-EP Turbo Unlock enabled; skipping microcode update\n");
			return 1;
		}
	}
	return 0;
}

#define GRANTLEY_DEFAULT_TOLUM  0x80000000UL

static void grantley_domain_read_resources(struct device *dev)
{
	int idx = 10;
	uint64_t low_ram_end;

	pci_domain_read_resources(dev);

	/* 0..0xa0000 DOS low RAM */
	ram_range(dev, idx++, 0, 0xa0000);
	/* 0xa0000..0xc0000 Video buffer / MMIO */
	mmio_from_to(dev, idx++, 0xa0000, 0xc0000);
	/* 0xc0000..0x100000 Option ROM / Upper memory */
	ram_from_to(dev, idx++, 0xc0000, 1 * MiB);

	uintptr_t tseg_base;
	size_t tseg_size;
	smm_region(&tseg_base, &tseg_size);

	/* Read hardware memory registers populated by uncore blob */
	const pci_devfn_t iio_dev = PCI_DEV(0, 5, 0);
	uint32_t tolm =
		pci_s_read_config32(iio_dev, 0xd0) & 0xfc000000;
	uint32_t tohm_lo =
		pci_s_read_config32(iio_dev, 0xd4) & 0xfc000000;
	uint32_t tohm_hi = pci_s_read_config32(iio_dev, 0xd8);
	uint64_t tohm = ((uint64_t)tohm_hi << 32) | tohm_lo;

	if (tolm >= 0x40000000 && tolm <= 0xd0000000)
		low_ram_end = tolm;
	else
		low_ram_end = GRANTLEY_DEFAULT_TOLUM;

	if (tseg_size > 0 && tseg_base < low_ram_end) {
		ram_from_to(dev, idx++, 1 * MiB, tseg_base);
		mmio_range(dev, idx++, tseg_base, tseg_size);
	} else {
		ram_from_to(dev, idx++, 1 * MiB, low_ram_end);
	}

	/* High DRAM above 4 GiB */
	if (tohm > 0x100000000ULL) {
		uint64_t high_ram_size = tohm - 0x100000000ULL;
		ram_range(dev, idx++, 0x100000000ULL, high_ram_size);
	}
}

static void grantley_domain_set_resources(struct device *dev)
{
	assign_resources(dev->downstream);
}

struct device_operations grantley_pci_domain_ops = {
	.read_resources   = grantley_domain_read_resources,
	.set_resources    = grantley_domain_set_resources,
	.scan_bus         = pci_host_bridge_scan_bus,
	.ops_pci          = &pci_dev_ops_pci,
};

void mp_init_cpus(DEVTREE_CONST struct bus *cpu_bus)
{
	struct grantley_cpu_params cpu_params;

	printk(BIOS_INFO, "Haswell-EP MP init: BSP bring-up\n");

	mainboard_cpu_init_params(&cpu_params);
	grantley_apply_cpu_overrides(&cpu_params);
}

struct device_operations grantley_cpu_bus_ops = {
	.read_resources   = noop_read_resources,
	.set_resources    = noop_set_resources,
	.init             = mp_cpu_bus_init,
};

struct chip_operations northbridge_intel_grantley_ops = {
	.name = "Intel Haswell-EP / Broadwell-EP Northbridge",
};
