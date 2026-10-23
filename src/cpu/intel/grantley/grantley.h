/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CPU_INTEL_GRANTLEY_H
#define CPU_INTEL_GRANTLEY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* CPU power and overclocking / undervolting parameters */
struct grantley_cpu_params {
	bool enable_turbo_unlock;      /* Haswell-EP v3 all-core turbo unlock */
	int16_t core_voltage_offset_mv;   /* Core offset: e.g. -50, -60, -70 mV */
	int16_t uncore_voltage_offset_mv; /* Uncore / Ring offset: e.g. -50 mV */
};

void mainboard_cpu_init_params(struct grantley_cpu_params *params);
void grantley_apply_cpu_overrides(const struct grantley_cpu_params *params);

/* Model classification only: not a claim of validated socket/stepping support. */
enum grantley_cpu {
	GRANTLEY_CPU_UNKNOWN,
	GRANTLEY_HASWELL_EP,
	GRANTLEY_BROADWELL_EP,
};

enum grantley_cpu grantley_cpu_model(uint32_t signature);

/* SMRR address fields on these processors are bits 31:12, not MAXPHYADDR. */
bool grantley_smrr_values(uint64_t base, uint64_t size, uint32_t *smrr_base,
			 uint32_t *smrr_mask);
void grantley_get_smm_info(uintptr_t *base, size_t *size, size_t *save_state_size);
void grantley_smm_initialize(void);
void grantley_smm_relocation_handler(int cpu, uintptr_t current_smbase,
				     uintptr_t new_smbase);
void enable_smm_code_access_check(void);
void grantley_smm_lock(void);

struct device;
void grantley_cpu_finalize(struct device *dev);
void grantley_cpu_fill_ssdt(const struct device *dev);

#endif
