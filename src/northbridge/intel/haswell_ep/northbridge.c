/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/helpers.h>
#include <console/console.h>
#include <device/device.h>
#include <device/pci.h>
#include <device/pci_ops.h>
#include <cpu/cpu.h>
#include <cpu/x86/smm.h>
#include <types.h>
#include "uncore_runner/pei_services.h"

#define GRANTLEY_DEFAULT_TOLUM  0x80000000UL

static void grantley_domain_read_resources(struct device *dev)
{
	int idx = 10;
	uint64_t low_ram_end;
	const struct grantley_uncore_state *state = grantley_get_uncore_state();

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

	if (state && state->dram_initialized && state->dram_size > 0) {
		low_ram_end = (state->dram_size < GRANTLEY_DEFAULT_TOLUM) ?
			state->dram_size : GRANTLEY_DEFAULT_TOLUM;
	} else {
		low_ram_end = GRANTLEY_DEFAULT_TOLUM;
	}

	if (tseg_size > 0 && tseg_base < low_ram_end) {
		ram_from_to(dev, idx++, 1 * MiB, tseg_base);
		mmio_range(dev, idx++, tseg_base, tseg_size);
	} else {
		ram_from_to(dev, idx++, 1 * MiB, low_ram_end);
	}

	/* High DRAM above 4 GiB */
	if (state && state->dram_initialized && state->dram_size > 0x100000000ULL) {
		uint64_t high_ram_size = state->dram_size - low_ram_end;
		ram_range(dev, idx++, 0x100000000ULL, high_ram_size);
	}
}

static void grantley_domain_set_resources(struct device *dev)
{
	assign_resources(dev->downstream);
}

static struct device_operations grantley_pci_domain_ops = {
	.read_resources   = grantley_domain_read_resources,
	.set_resources    = grantley_domain_set_resources,
	.scan_bus         = pci_host_bridge_scan_bus,
	.ops_pci          = &pci_dev_ops_pci,
};

static struct device_operations grantley_cpu_bus_ops = {
	.read_resources   = noop_read_resources,
	.set_resources    = noop_set_resources,
	.init             = mp_cpu_bus_init,
};

static struct chip_operations northbridge_intel_haswell_ep_ops = {
	.name = "Intel Haswell-EP / Broadwell-EP Northbridge",
};
