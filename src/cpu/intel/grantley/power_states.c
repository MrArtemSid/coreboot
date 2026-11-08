/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <acpi/acpi.h>
#include <acpi/acpigen.h>
#include <commonlib/helpers.h>
#include <console/console.h>
#include <cpu/cpu.h>
#include <cpu/intel/msr.h>
#include <cpu/intel/turbo.h>
#include <cpu/x86/msr.h>
#include "grantley.h"

#define PSS_MAX_ENTRIES		16
#define PSS_RATIO_STEP		1
#define PSS_LATENCY_TRANSITION	10
#define PSS_LATENCY_BUSMASTER	10

#define MSR_MISC_PWR_MGMT	0x1aa
#define MSR_TURBO_RATIO_LIMIT	0x1ad
#define MSR_PKG_POWER_SKU_UNIT	0x606
#define MSR_PKG_POWER_SKU	0x614
#define MSR_CONFIG_TDP_NOMINAL	0x648

static int cpu_get_coord_type(void)
{
	return rdmsr(MSR_MISC_PWR_MGMT).lo & BIT(0) ? SW_ANY : HW_ALL;
}

static u32 cpu_get_min_ratio(void)
{
	return (rdmsr(MSR_PLATFORM_INFO).hi >> 8) & 0xff;
}

static u32 cpu_get_max_ratio(void)
{
	const msr_t platform = rdmsr(MSR_PLATFORM_INFO);

	if ((platform.hi >> 1) & 3) {
		const u32 nominal = rdmsr(MSR_CONFIG_TDP_NOMINAL).lo & 0xff;

		if (nominal)
			return nominal;
	}
	return (platform.lo >> 8) & 0xff;
}

static u32 cpu_get_bus_clock(void)
{
	return 100 * KHz;
}

static u32 cpu_get_power_max(void)
{
	const u32 unit = rdmsr(MSR_PKG_POWER_SKU_UNIT).lo & 0xf;
	const u32 power = rdmsr(MSR_PKG_POWER_SKU).lo & 0x7fff;

	return (power * 1000) >> unit;
}

static u32 cpu_get_max_turbo_ratio(void)
{
	/*
	 * Bits 7:0 hold the one-active-core turbo ratio on Haswell-EP and
	 * Broadwell-EP alike; the upper bits describe more active cores.
	 */
	return rdmsr(MSR_TURBO_RATIO_LIMIT).lo & 0xff;
}

/* Duty-cycle percentages and controls follow the stock Grantley SSDT. */
static acpi_tstate_t tss_table_fine[] = {
	{ 100, 1000, 0, 0x00, 0 },
	{ 94, 940, 0, 0x1f, 0 },
	{ 88, 880, 0, 0x1e, 0 },
	{ 82, 820, 0, 0x1d, 0 },
	{ 75, 760, 0, 0x1c, 0 },
	{ 69, 700, 0, 0x1b, 0 },
	{ 63, 640, 0, 0x1a, 0 },
	{ 57, 580, 0, 0x19, 0 },
	{ 50, 520, 0, 0x18, 0 },
	{ 44, 460, 0, 0x17, 0 },
	{ 38, 400, 0, 0x16, 0 },
	{ 32, 340, 0, 0x15, 0 },
	{ 25, 280, 0, 0x14, 0 },
	{ 19, 220, 0, 0x13, 0 },
	{ 13, 160, 0, 0x12, 0 },
};

static acpi_tstate_t tss_table_coarse[] = {
	{ 100, 1000, 0, 0x00, 0 },
	{ 88, 875, 0, 0x1f, 0 },
	{ 75, 750, 0, 0x1e, 0 },
	{ 63, 625, 0, 0x1d, 0 },
	{ 50, 500, 0, 0x1c, 0 },
	{ 38, 375, 0, 0x1b, 0 },
	{ 25, 250, 0, 0x1a, 0 },
	{ 13, 125, 0, 0x19, 0 },
};

static int common_calculate_power_ratio(int tdp, int p1_ratio, int ratio)
{
	u32 m;
	u32 power;

	/*
	 * M = ((1.1 - ((p1_ratio - ratio) * 0.00625)) / 1.1) ^ 2
	 *
	 * Power = (ratio / p1_ratio) * m * tdp
	 */

	m = (110000 - ((p1_ratio - ratio) * 625)) / 11;
	m = (m * m) / 1000;

	power = ((ratio * 100000 / p1_ratio) / 100);
	power *= (m / 100) * (tdp / 1000);
	power /= 1000;

	return power;
}

static void generate_p_state_entries(int core, int cores_per_package)
{
	int ratio_min, ratio_max, ratio_turbo, ratio_step;
	int coord_type, power_max, num_entries;
	int ratio, power, clock, clock_max;
	bool turbo;

	coord_type = cpu_get_coord_type();
	ratio_min = cpu_get_min_ratio();
	ratio_max = cpu_get_max_ratio();
	if (!ratio_min || ratio_max < ratio_min) {
		printk(BIOS_ERR, "Grantley: invalid P-state ratios %d-%d\n",
		       ratio_min, ratio_max);
		return;
	}
	clock_max = (ratio_max * cpu_get_bus_clock()) / KHz;
	turbo = (get_turbo_state() == TURBO_ENABLED);

	if (turbo) {
		ratio_turbo = cpu_get_max_turbo_ratio();
		/* A turbo ratio at or below the base ratio is not a P-state. */
		if (ratio_turbo <= ratio_max) {
			printk(BIOS_WARNING,
			       "Grantley: turbo ratio %d does not exceed %d, not advertised\n",
			       ratio_turbo, ratio_max);
			turbo = false;
		}
	}

	/* Calculate CPU TDP in mW */
	power_max = cpu_get_power_max();

	/* Write _PCT indicating use of FFixedHW */
	acpigen_write_empty_PCT();

	/* Write _PPC with no limit on supported P-state */
	acpigen_write_PPC_NVS();
	/* Write PSD indicating configured coordination type */
	acpigen_write_PSD_package(core, 1, coord_type);

	/* Add P-state entries in _PSS table */
	acpigen_write_name("_PSS");

	/* Determine ratio points */
	ratio_step = PSS_RATIO_STEP;
	do {
		num_entries = ((ratio_max - ratio_min) / ratio_step) + 1;
		if (((ratio_max - ratio_min) % ratio_step) > 0)
			num_entries += 1;
		if (turbo)
			num_entries += 1;
		if (num_entries > PSS_MAX_ENTRIES)
			ratio_step += 1;
	} while (num_entries > PSS_MAX_ENTRIES);

	/* _PSS package count depends on Turbo */
	acpigen_write_package(num_entries);

	/* P[T] is Turbo state if enabled */
	if (turbo) {
		clock = (ratio_turbo * cpu_get_bus_clock()) / KHz;

		/* Add entry for Turbo ratio */
		acpigen_write_PSS_package(clock,		/* MHz */
					  power_max,		/* mW */
					  PSS_LATENCY_TRANSITION,/* lat1 */
					  PSS_LATENCY_BUSMASTER,/* lat2 */
					  ratio_turbo << 8,	/* control */
					  ratio_turbo << 8);	/* status */
		num_entries -= 1;
	}

	/* First regular entry is max non-turbo ratio */
	acpigen_write_PSS_package(clock_max,		/* MHz */
				  power_max,		/* mW */
				  PSS_LATENCY_TRANSITION,/* lat1 */
				  PSS_LATENCY_BUSMASTER,/* lat2 */
				  ratio_max << 8,	/* control */
				  ratio_max << 8);	/* status */
	num_entries -= 1;

	/* Generate the remaining entries */
	for (ratio = ratio_min + ((num_entries - 1) * ratio_step);
	     ratio >= ratio_min; ratio -= ratio_step) {
		/* Calculate power at this ratio */
		power = common_calculate_power_ratio(power_max, ratio_max, ratio);
		clock = (ratio * cpu_get_bus_clock()) / KHz;

		acpigen_write_PSS_package(clock,		/* MHz */
					  power,		/* mW */
					  PSS_LATENCY_TRANSITION,/* lat1 */
					  PSS_LATENCY_BUSMASTER,/* lat2 */
					  ratio << 8,		/* control */
					  ratio << 8);		/* status */
	}
	/* Fix package length */
	acpigen_pop_len();
}

static acpi_tstate_t *soc_get_tss_table(int *entries)
{
	if (cpuid_eax(6) & BIT(5)) {
		*entries = ARRAY_SIZE(tss_table_fine);
		return tss_table_fine;
	}
	*entries = ARRAY_SIZE(tss_table_coarse);
	return tss_table_coarse;
}

static void generate_t_state_entries(int core, int cores_per_package)
{
	acpi_tstate_t *soc_tss_table;
	int entries;

	soc_tss_table = soc_get_tss_table(&entries);
	if (entries == 0)
		return;

	/* Indicate SW_ALL coordination for T-states */
	acpigen_write_TSD_package(core, cores_per_package, SW_ALL);

	/* Indicate FixedHW so OS will use MSR */
	acpigen_write_empty_PTC();

	/* Set NVS controlled T-state limit */
	acpigen_write_TPC("\\TLVL");

	/* Write TSS table for MSR access */
	acpigen_write_TSS_package(entries, soc_tss_table);
}

void grantley_generate_power_states(int core, int cores_per_package)
{
	grantley_generate_c_state_entries();
	generate_p_state_entries(core, cores_per_package);
	generate_t_state_entries(core / cores_per_package, cores_per_package);
}
