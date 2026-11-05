/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <acpi/acpigen.h>
#include <console/console.h>
#include <cpu/cpu.h>
#include <cpu/intel/turbo.h>
#include <cpu/x86/msr.h>
#include <device/device.h>
#include <types.h>
#include "grantley.h"

#define CPU_BCLK			100	/* MHz */

/* P-state configuration */
#define PSS_MAX_ENTRIES			8
#define PSS_RATIO_STEP			2
#define PSS_LATENCY_TRANSITION		10
#define PSS_LATENCY_BUSMASTER		10

/* Power management MSRs, architectural but not in cpu/x86/msr.h */
#define MSR_PLATFORM_INFO		0xce
#define MSR_PERF_STATUS			0x198
#define MSR_MISC_PWR_MGMT		0x1aa
#define  MISC_PWR_MGMT_EIST_HW_DIS	(1 << 0)
#define MSR_TURBO_RATIO_LIMIT		0x1ad
#define MSR_PKG_POWER_SKU_UNIT		0x606
#define MSR_PKG_POWER_SKU		0x614
#define MSR_CONFIG_TDP_NOMINAL		0x648

/* MSR_PLATFORM_INFO fields */
#define PLATFORM_INFO_MAX_NON_TURBO_RATIO(lo)	(((lo) >> 8) & 0xff)
#define PLATFORM_INFO_MAX_EFFICIENCY_RATIO(hi)	(((hi) >> 8) & 0xff)
#define PLATFORM_INFO_CONFIG_TDP_LEVELS(hi)	(((hi) >> 1) & 0x3)

/* Intel suggested C-state exit latency in units of 1024 ns. */
#define C_STATE_LATENCY_CONTROL_0_LIMIT	0x42
#define C_STATE_LATENCY_CONTROL_2_LIMIT	0x91
#define C_STATE_LATENCY_MICRO_SECONDS(limit)	(((1 << 5) * (limit)) / 1000)

#define MWAIT_RES(state, sub_state) {					\
	.addrl = (((state) << 4) | (sub_state)),			\
	.space_id = ACPI_ADDRESS_SPACE_FIXED,				\
	.bit_width = ACPI_FFIXEDHW_VENDOR_INTEL,			\
	.bit_offset = ACPI_FFIXEDHW_CLASS_MWAIT,			\
	.access_size = ACPI_FFIXEDHW_FLAG_HW_COORD,			\
}

static const acpi_cstate_t cstate_map[] = {
	{
		.ctype = 1,
		.power = 1000,
		.resource = MWAIT_RES(0, 0),
	},
	{
		.ctype = 2,
		.latency = C_STATE_LATENCY_MICRO_SECONDS(C_STATE_LATENCY_CONTROL_0_LIMIT),
		.power = 900,
		.resource = MWAIT_RES(1, 0),
	},
	{
		.ctype = 3,
		.latency = C_STATE_LATENCY_MICRO_SECONDS(C_STATE_LATENCY_CONTROL_2_LIMIT),
		.power = 800,
		.resource = MWAIT_RES(2, 1),
	},
};

static const acpi_tstate_t tss_table_fine[] = {
	{ 100, 1000, 0, 0x1f, 0 },
	{ 88, 875, 0, 0x1e, 0 },
	{ 75, 750, 0, 0x1d, 0 },
	{ 63, 625, 0, 0x1c, 0 },
	{ 50, 500, 0, 0x1b, 0 },
	{ 38, 375, 0, 0x1a, 0 },
	{ 25, 250, 0, 0x19, 0 },
	{ 13, 125, 0, 0x18, 0 },
};

static const acpi_tstate_t tss_table_coarse[] = {
	{ 100, 1000, 0, 0x00, 0 },
	{ 88, 875, 0, 0x1f, 0 },
	{ 75, 750, 0, 0x1e, 0 },
	{ 63, 625, 0, 0x1d, 0 },
	{ 50, 500, 0, 0x1c, 0 },
	{ 38, 375, 0, 0x1b, 0 },
	{ 25, 250, 0, 0x1a, 0 },
	{ 13, 125, 0, 0x19, 0 },
};

static int calculate_power(int tdp, int p1_ratio, int ratio)
{
	u32 m, power;

	/*
	 * M = ((1.1 - ((p1_ratio - ratio) * 0.00625)) / 1.1) ^ 2
	 * Power = (ratio / p1_ratio) * m * tdp
	 */
	m = (110000 - ((p1_ratio - ratio) * 625)) / 11;
	m = (m * m) / 1000;

	power = (ratio * 100000 / p1_ratio) / 100;
	power *= (m / 100) * (tdp / 1000);
	power /= 1000;

	return (int)power;
}

static void generate_p_state_entries(int core)
{
	int ratio_min, ratio_max, ratio_turbo, ratio_step;
	int coord_type, power_max, power_unit, num_entries;
	int ratio, power, clock, clock_max;
	msr_t msr;

	/* Coordination follows the EIST configuration the module left behind. */
	msr = rdmsr(MSR_MISC_PWR_MGMT);
	coord_type = (msr.lo & MISC_PWR_MGMT_EIST_HW_DIS) ? SW_ANY : HW_ALL;

	msr = rdmsr(MSR_PLATFORM_INFO);
	ratio_min = PLATFORM_INFO_MAX_EFFICIENCY_RATIO(msr.hi);
	if (PLATFORM_INFO_CONFIG_TDP_LEVELS(msr.hi))
		ratio_max = rdmsr(MSR_CONFIG_TDP_NOMINAL).lo & 0xff;
	else
		ratio_max = PLATFORM_INFO_MAX_NON_TURBO_RATIO(msr.lo);
	clock_max = ratio_max * CPU_BCLK;

	/* Package power limit, in units of 1/2^n W from the SKU MSR. */
	msr = rdmsr(MSR_PKG_POWER_SKU_UNIT);
	power_unit = msr.lo & 0xf;
	msr = rdmsr(MSR_PKG_POWER_SKU);
	power_max = (int)(msr.lo & 0x7fff) * 1000 / (2 << (power_unit - 1));

	if (!core) {
		msr = rdmsr(MSR_PERF_STATUS);
		printk(BIOS_DEBUG, "Grantley: P-states %d-%d x %d MHz, %d mW, "
		       "PERF_STATUS %08x:%08x\n", ratio_min, ratio_max, CPU_BCLK,
		       power_max, msr.hi, msr.lo);
	}

	/* Write _PCT indicating use of FFixedHW */
	acpigen_write_empty_PCT();

	/* Write _PPC with no limit on supported P-state */
	acpigen_write_PPC(0);

	/* Write PSD indicating configured coordination type */
	acpigen_write_PSD_package(core, 1, coord_type);

	/* Add P-state entries in _PSS table */
	acpigen_write_name("_PSS");

	/* Determine ratio points */
	ratio_step = PSS_RATIO_STEP;
	num_entries = (ratio_max - ratio_min) / ratio_step;
	while (num_entries > PSS_MAX_ENTRIES - 1) {
		ratio_step <<= 1;
		num_entries >>= 1;
	}

	/* P[T] is Turbo state if enabled */
	if (get_turbo_state() == TURBO_ENABLED) {
		/* _PSS package count including Turbo */
		acpigen_write_package(num_entries + 2);

		msr = rdmsr(MSR_TURBO_RATIO_LIMIT);
		ratio_turbo = msr.lo & 0xff;

		/* Add entry for Turbo ratio */
		acpigen_write_PSS_package(
			clock_max + 1,		/* MHz */
			power_max,		/* mW */
			PSS_LATENCY_TRANSITION,	/* lat1 */
			PSS_LATENCY_BUSMASTER,	/* lat2 */
			ratio_turbo << 8,	/* control */
			ratio_turbo << 8);	/* status */
	} else {
		/* _PSS package count without Turbo */
		acpigen_write_package(num_entries + 1);
	}

	/* First regular entry is max non-turbo ratio */
	acpigen_write_PSS_package(
		clock_max,		/* MHz */
		power_max,		/* mW */
		PSS_LATENCY_TRANSITION,	/* lat1 */
		PSS_LATENCY_BUSMASTER,	/* lat2 */
		ratio_max << 8,		/* control */
		ratio_max << 8);	/* status */

	/* Generate the remaining entries */
	for (ratio = ratio_min + ((num_entries - 1) * ratio_step);
	     ratio >= ratio_min; ratio -= ratio_step) {
		/* Calculate power at this ratio */
		power = calculate_power(power_max, ratio_max, ratio);
		clock = ratio * CPU_BCLK;

		acpigen_write_PSS_package(
			clock,			/* MHz */
			power,			/* mW */
			PSS_LATENCY_TRANSITION,	/* lat1 */
			PSS_LATENCY_BUSMASTER,	/* lat2 */
			ratio << 8,		/* control */
			ratio << 8);		/* status */
	}

	/* Fix package length */
	acpigen_pop_len();
}

static void generate_c_state_entries(void)
{
	acpigen_write_CST_package(cstate_map, ARRAY_SIZE(cstate_map));
}

static void generate_t_state_entries(int core, int cores_per_package)
{
	/* Indicate SW_ALL coordination for T-states */
	acpigen_write_TSD_package(core, cores_per_package, SW_ALL);

	/* Indicate FFixedHW so the OS uses the MSR */
	acpigen_write_empty_PTC();

	/* Set a T-state limit that can be modified in NVS */
	acpigen_write_TPC("\\TLVL");

	/*
	 * CPUID.(EAX=6):EAX[5] indicates support for extended throttle
	 * levels. The stock firmware publishes both and leaves the choice
	 * to the OS.
	 */
	if (cpuid_eax(6) & (1 << 5))
		acpigen_write_TSS_package(ARRAY_SIZE(tss_table_fine),
					  (acpi_tstate_t *)tss_table_fine);
	else
		acpigen_write_TSS_package(ARRAY_SIZE(tss_table_coarse),
					  (acpi_tstate_t *)tss_table_coarse);
}

void grantley_cpu_fill_ssdt(const struct device *dev)
{
	const unsigned int count = dev_count_cpu();

	/* MADT uses contiguous processor UIDs for enabled CPU devices as well. */
	for (unsigned int i = 0; i < count; i++) {
		acpigen_write_processor(i, 0, 0);
		generate_p_state_entries(i);
		generate_c_state_entries();
		generate_t_state_entries(i, 1);
		acpigen_pop_len();
	}
	acpigen_write_processor_package("PPKG", 0, count);
	acpigen_write_processor_cnot(count);
}
