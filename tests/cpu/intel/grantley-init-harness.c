/* SPDX-License-Identifier: GPL-2.0-only */

#include "grantley-init-shim.h"
void grantley_cpu_finalize(struct device *dev);
void grantley_cpu_finalize(struct device *dev) { (void)dev; }
#include <cpu/intel/grantley/cpu_init.c>

static msr_t thread_count = { .lo = (22 << 16) | 44 };
static msr_t misc_enable;
static uint32_t features, leaf1_edx;
static int mtrr_setup, mtrr_check, vmx, dca, epb, override, mailbox, relocated;
static int microcode_loads, skip_update, fail_mp;
static bool expect_die;
static jmp_buf halt_target;
static const uint32_t patch[2] = {1, 2};
static const struct mp_ops *captured_ops;

uint32_t cpu_get_feature_flags_ecx(void) { return features; }
uint32_t cpuid_edx(unsigned int leaf) { assert(leaf == 1); return leaf1_edx; }
msr_t rdmsr(unsigned int reg)
{
	if (reg == 0x35)
		return thread_count;
	assert(reg == IA32_MISC_ENABLE);
	return misc_enable;
}
void wrmsr(unsigned int reg, msr_t value)
{
	assert(reg == IA32_MISC_ENABLE);
	misc_enable = value;
}
void set_vmx_and_lock(void) { vmx++; }
void configure_dca_cap(void) { dca++; }
void set_energy_perf_bias(uint8_t policy) { assert(policy == 6); epb++; }
void x86_setup_mtrrs_with_detect(void) { mtrr_setup++; }
void x86_mtrr_check(void) { mtrr_check++; }
void program_oc_mailbox(void) { mailbox++; }
void grantley_apply_cpu_overrides(const struct grantley_cpu_params *params)
{
	assert(!params->enable_turbo_unlock);
	assert(!params->core_voltage_offset_mv);
	override++;
}
const void *intel_microcode_find(void) { return patch; }
void intel_microcode_load_unlocked(const void *data)
{
	assert(data == patch);
	microcode_loads++;
}
uint32_t get_microcode_size(const void *data) { assert(data == patch); return 4096; }
uint32_t get_microcode_rev(const void *data) { assert(data == patch); return 2; }
uint32_t get_current_microcode_rev(void) { return 1; }
int soc_skip_ucode_update(uint32_t current, uint32_t next)
{
	assert(current == 1 && next == 2);
	return skip_update;
}
void smm_initiate_relocation(void) { relocated++; }
void grantley_get_smm_info(uintptr_t *base, size_t *size, size_t *state_size)
{
	(void)base; (void)size; (void)state_size;
}
void grantley_smm_initialize(void) {}
void grantley_smm_relocation_handler(int cpu, uintptr_t old, uintptr_t next)
{
	(void)cpu; (void)old; (void)next;
}
void grantley_smm_lock(void) {}
enum cb_err mp_init_with_smm(struct bus *bus, const struct mp_ops *ops)
{
	assert(bus != NULL);
	captured_ops = ops;
	return fail_mp ? CB_ERR : CB_SUCCESS;
}
void printk(int level, const char *format, ...) { (void)level; (void)format; }
_Noreturn void die(const char *message)
{
	(void)message;
	assert(expect_die);
	longjmp(halt_target, 1);
}

int main(void)
{
	struct bus bus;
	const void *data;
	size_t size;
	int parallel;

	mp_init_cpus(&bus);
	assert(captured_ops);
	captured_ops->pre_mp_init();
	assert(mtrr_setup == 1 && mtrr_check == 1 && override == 1 && mailbox == 1);
	assert(microcode_loads == 1);
	assert(captured_ops->get_cpu_count() == 44);
	thread_count.lo = (12 << 16) | 12;
	assert(captured_ops->get_cpu_count() == 12);
	const uint32_t invalid[] = {0, (22 << 16) | 45, (2 << 16) | 1, (2 << 16) | 5};
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		thread_count.lo = invalid[i];
		expect_die = true;
		if (!setjmp(halt_target)) {
			captured_ops->get_cpu_count();
			abort();
		}
		expect_die = false;
	}
	captured_ops->get_microcode_info(&data, &size, &parallel);
	assert(data == patch && size == 4096 && !parallel);
	skip_update = 1;
	captured_ops->get_microcode_info(&data, &size, &parallel);
	assert(data == NULL && !size && !parallel);

	misc_enable.raw = 1ULL << 40;
	grantley_cpu_driver.ops->init(NULL);
	assert(misc_enable.raw == ((1ULL << 40) | FAST_STRINGS_ENABLE_BIT));
	features = 1 << 7;
	leaf1_edx = 1U << 29;
	grantley_cpu_driver.ops->init(NULL);
	assert((misc_enable.lo & (SPEED_STEP_ENABLE_BIT | TM1_TM2_EMTTM_ENABLE_BIT)) ==
	       (SPEED_STEP_ENABLE_BIT | TM1_TM2_EMTTM_ENABLE_BIT));
	assert(misc_enable.hi == 1 << 8 && vmx == 2 && dca == 2 && epb == 2);
#if CONFIG(HAVE_SMI_HANDLER)
	assert(captured_ops->get_smm_info == grantley_get_smm_info);
	assert(captured_ops->relocation_handler == grantley_smm_relocation_handler);
	assert(captured_ops->post_mp_init == grantley_smm_lock);
	captured_ops->per_cpu_smm_trigger();
	assert(relocated == 1 && microcode_loads == 2);
#endif
	fail_mp = 1;
	expect_die = true;
	if (!setjmp(halt_target)) {
		mp_init_cpus(&bus);
		abort();
	}
	puts("Grantley CPU callbacks: PASS");
	return 0;
}
