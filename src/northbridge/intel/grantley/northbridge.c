/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/helpers.h>
#include <console/console.h>
#include <device/device.h>
#include <device/pci.h>
#include <device/pci_ops.h>
#include <cpu/cpu.h>
#include <cpu/intel/grantley/grantley.h>
#include <cpu/intel/microcode.h>
#include <cpu/x86/msr.h>
#include <cpu/x86/smm.h>
#include <drivers/intel/oc_mailbox/oc_mailbox.h>
#include <types.h>
#include "platform.h"
#include "memmap.h"

#define OC_MBOX_PLANE_CORE		0
#define OC_MBOX_PLANE_CACHE		2
#define OC_MBOX_CMD_OVERRIDE_WRITE	0x11

static void set_voltage_offset(uint8_t plane, int16_t offset_mv)
{
	const union oc_mailbox_interface cmd = {
		.cmd_code = OC_MBOX_CMD_OVERRIDE_WRITE,
		.param1   = plane,
	};
	/* FIVR offset is 11-bit signed fixed point in units of 1/1024 V */
	int32_t val = ((int32_t)offset_mv * 1024) / 1000;
	uint32_t data = ((uint32_t)val & 0x7ff) << 21;

	printk(BIOS_INFO,
	       "Grantley: Setting plane %u voltage offset %d mV (data 0x%08x)\n",
	       plane, offset_mv, data);
	if (oc_mailbox_write(cmd, data) < 0)
		printk(BIOS_ERR,
		       "Grantley: Failed to write OC mailbox for plane %u\n",
		       plane);
}

void grantley_apply_cpu_overrides(const struct grantley_cpu_params *params)
{
	if (!params)
		return;

	if (params->core_voltage_offset_mv != 0)
		set_voltage_offset(OC_MBOX_PLANE_CORE,
				   params->core_voltage_offset_mv);

	if (params->uncore_voltage_offset_mv != 0)
		set_voltage_offset(OC_MBOX_PLANE_CACHE,
				   params->uncore_voltage_offset_mv);
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

static void grantley_domain_read_resources(struct device *dev)
{
	const struct grantley_memory_map *map = grantley_get_memory_map();
	int idx = 10;

	pci_domain_read_resources(dev);
	mmio_from_to(dev, idx++, 0xa0000, 0xc0000);

	for (unsigned int i = 0; i < map->count; i++) {
		const struct grantley_memory_range *range = &map->ranges[i];
		uint64_t end = range->base + range->size;
		uint64_t base = range->base;
		bool reserved = range->type == GRANTLEY_MEMORY_RESERVED_RAM;

		/* Do not turn the VGA aperture or option-ROM area into allocatable RAM. */
		if (base < 0xa0000) {
			if (reserved)
				reserved_ram_from_to(dev, idx++, base, MIN(end, 0xa0000));
			else
				ram_from_to(dev, idx++, base, MIN(end, 0xa0000));
		}
		if (base < MiB && end > 0xc0000)
			reserved_ram_from_to(dev, idx++, MAX(base, 0xc0000), MIN(end, MiB));
		base = MAX(base, MiB);
		if (base >= end)
			continue;
		/* Reserved DRAM, including SMRAM, remains WB for x86_setup_mtrrs(). */
		if (reserved)
			reserved_ram_from_to(dev, idx++, base, end);
		else
			ram_from_to(dev, idx++, base, end);
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

struct chip_operations northbridge_intel_grantley_ops = {
	.name = "Intel Haswell-EP / Broadwell-EP Northbridge",
};
