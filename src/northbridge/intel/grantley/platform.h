/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NORTHBRIDGE_INTEL_GRANTLEY_PLATFORM_H
#define NORTHBRIDGE_INTEL_GRANTLEY_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

struct grantley_memory_params {
	uint16_t max_dram_speed_mts; /* 0 = Auto/SPD, 1600, 1866, 2133, 2400 */
	uint8_t tcl;                 /* 0 = Auto */
	uint8_t trcd;                /* 0 = Auto */
	uint8_t trp;                 /* 0 = Auto */
	uint8_t tras;                /* 0 = Auto */
	uint8_t command_rate;        /* 0 = Auto, 1 = 1T, 2 = 2T */
	bool ecc_en;                 /* Force ECC enable */
};

/* CPU power and overclocking / undervolting parameters */
struct grantley_cpu_params {
	bool enable_turbo_unlock;      /* Haswell-EP v3 all-core turbo unlock */
	int16_t core_voltage_offset_mv;   /* Core offset: e.g. -50, -60, -70 mV */
	int16_t uncore_voltage_offset_mv; /* Uncore / Ring offset: e.g. -50 mV */
};

/* Optional mainboard hooks */
void mainboard_memory_init_params(struct grantley_memory_params *params);
void mainboard_cpu_init_params(struct grantley_cpu_params *params);
void grantley_apply_cpu_overrides(const struct grantley_cpu_params *params);

/* Model classification only: not a claim of validated socket/stepping support. */
enum grantley_cpu {
	GRANTLEY_CPU_UNKNOWN,
	GRANTLEY_HASWELL_EP,
	GRANTLEY_BROADWELL_EP,
};

enum grantley_cpu grantley_cpu_model(uint32_t signature);

/* Supported PCH device IDs on Grantley hybrid / native platforms. */
enum grantley_pch_device {
	GRANTLEY_PCH_Z87         = 0x8c44,
	GRANTLEY_PCH_H87         = 0x8c4a,
	GRANTLEY_PCH_Q85         = 0x8c4c,
	GRANTLEY_PCH_Q87         = 0x8c4e,
	GRANTLEY_PCH_B85         = 0x8c50,
	GRANTLEY_PCH_C222        = 0x8c52,
	GRANTLEY_PCH_C224        = 0x8c54,
	GRANTLEY_PCH_C226        = 0x8c56,
	GRANTLEY_PCH_H81         = 0x8c5c,
	GRANTLEY_PCH_Z97         = 0x8cc4,
	GRANTLEY_PCH_H97         = 0x8cc6,
	GRANTLEY_PCH_WBG_MIN     = 0x8d40,
	GRANTLEY_PCH_WBG_MAX     = 0x8d5f,
	GRANTLEY_PCH_C610        = 0x8d40,
	GRANTLEY_PCH_C612        = 0x8d44,
	GRANTLEY_PCH_X99_HEDT    = 0x8d47,
	GRANTLEY_PCH_X99         = 0x8d4a,
	GRANTLEY_PCH_X99_ALT     = 0x8d50,
};

/* PCH compatibility check for Grantley platform (Lynx Point, Wildcat Point, Wellsburg). */
bool grantley_pch_is_supported(uint16_t vendor, uint16_t device);

#endif
