/* SPDX-License-Identifier: GPL-2.0-only */
/* Host-only substitutes for privileged hardware operations and MP services. */
#ifndef GRANTLEY_TEST_SHIM_H
#define GRANTLEY_TEST_SHIM_H
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdio.h>

#define CONFIG(x) CONFIG_##x
#ifndef CONFIG_HAVE_SMI_HANDLER
#define CONFIG_HAVE_SMI_HANDLER 1
#endif
#define CONFIG_MAX_CPUS 44
#define BIOS_DEBUG 0
#define BIOS_WARNING 1
#define __cpu_driver __attribute__((used))
#define X86_VENDOR_INTEL 0x8086
#define CPUID_ALL_STEPPINGS_MASK 0xfffffff0
#define CPU_TABLE_END {0, 0, 0}
#define IA32_MISC_ENABLE 0x1a0
#define FAST_STRINGS_ENABLE_BIT (1 << 0)
#define TM1_TM2_EMTTM_ENABLE_BIT (1 << 3)
#define SPEED_STEP_ENABLE_BIT (1 << 16)
#define ENERGY_POLICY_NORMAL 6
#define __weak __attribute__((weak))
typedef union { struct { uint32_t lo, hi; }; uint64_t raw; } msr_t;
struct device { int unused; };
struct bus { int unused; };
struct device_operations {
	void (*read_resources)(struct device *dev);
	void (*set_resources)(struct device *dev);
	void (*init)(struct device *dev);
	void (*final)(struct device *dev);
	void (*acpi_fill_ssdt)(const struct device *dev);
};
static inline void noop_read_resources(struct device *dev) { (void)dev; }
static inline void noop_set_resources(struct device *dev) { (void)dev; }
static inline void mp_cpu_bus_init(struct device *dev) { (void)dev; }
static inline void grantley_cpu_finalize_dummy(struct device *dev) { (void)dev; }
static inline void grantley_cpu_fill_ssdt(const struct device *dev) { (void)dev; }
struct cpu_device_id { unsigned int vendor, device, mask; };
struct cpu_driver { struct device_operations *ops; const struct cpu_device_id *id_table; };
enum cb_err { CB_SUCCESS, CB_ERR };
struct mp_ops {
	void (*pre_mp_init)(void);
	int (*get_cpu_count)(void);
	void (*get_microcode_info)(const void **, size_t *, int *);
	void (*get_smm_info)(uintptr_t *, size_t *, size_t *);
	void (*pre_mp_smm_init)(void);
	void (*per_cpu_smm_trigger)(void);
	void (*relocation_handler)(int, uintptr_t, uintptr_t);
	void (*post_mp_init)(void);
};
uint32_t cpu_get_feature_flags_ecx(void);
uint32_t cpuid_edx(unsigned int leaf);
msr_t rdmsr(unsigned int reg);
void wrmsr(unsigned int reg, msr_t value);
void set_vmx_and_lock(void);
void configure_dca_cap(void);
void set_energy_perf_bias(uint8_t policy);
void x86_setup_mtrrs_with_detect(void);
void x86_mtrr_check(void);
void program_oc_mailbox(void);
const void *intel_microcode_find(void);
void intel_microcode_load_unlocked(const void *patch);
uint32_t get_microcode_size(const void *patch);
uint32_t get_microcode_rev(const void *patch);
uint32_t get_current_microcode_rev(void);
int soc_skip_ucode_update(uint32_t current, uint32_t next);
void smm_initiate_relocation(void);
enum cb_err mp_init_with_smm(struct bus *bus, const struct mp_ops *ops);
void mp_init_cpus(struct bus *bus);
void printk(int level, const char *format, ...);
_Noreturn void die(const char *message);
#endif
