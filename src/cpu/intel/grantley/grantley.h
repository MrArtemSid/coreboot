/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CPU_INTEL_GRANTLEY_H
#define CPU_INTEL_GRANTLEY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void grantley_configure_cpu_pm(void);
bool grantley_cpu_pm_supported(void);
void grantley_configure_package_power_limit(void);

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
void grantley_generate_c_state_entries(void);
void grantley_generate_power_states(int core, int cores_per_package);

#endif
