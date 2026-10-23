/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <cpu/cpu.h>
#include <cpu/intel/common/common.h>
#include <cpu/intel/microcode.h>
#include <cpu/x86/mp.h>
#include <cpu/x86/msr.h>
#include <cpu/x86/mtrr.h>
#include <device/device.h>
#include <drivers/intel/oc_mailbox/oc_mailbox.h>
#include "grantley.h"

/* MSR_CORE_THREAD_COUNT is package scoped on Xeon E5 v3/v4. */
#define MSR_CORE_THREAD_COUNT 0x35

static void grantley_core_init(struct device *cpu)
{
	const uint32_t features = cpu_get_feature_flags_ecx();
	msr_t misc = rdmsr(IA32_MISC_ENABLE);

	misc.lo |= FAST_STRINGS_ENABLE_BIT;
	if (cpuid_edx(1) & (1 << 29))
		misc.lo |= TM1_TM2_EMTTM_ENABLE_BIT;
	if (features & (1 << 7))
		misc.lo |= SPEED_STEP_ENABLE_BIT;
	wrmsr(IA32_MISC_ENABLE, misc);

	/* These common helpers test CPUID and respect locked feature control. */
	set_vmx_and_lock();
	configure_dca_cap();
	set_energy_perf_bias(ENERGY_POLICY_NORMAL);
	/* Preserve machine-check records and the trained package power policy. */
}

static void grantley_pre_mp_init(void)
{
	struct grantley_cpu_params params = { 0 };

	/* Cache the patch on the BSP before APs can enter intel_microcode_find(). */
	intel_microcode_load_unlocked(intel_microcode_find());
	/* Common MP startup snapshots these MTRRs and installs them on each AP. */
	x86_setup_mtrrs_with_detect();
	x86_mtrr_check();
	mainboard_cpu_init_params(&params);
	grantley_apply_cpu_overrides(&params);
	program_oc_mailbox();
}

static int grantley_get_cpu_count(void)
{
	const msr_t count = rdmsr(MSR_CORE_THREAD_COUNT);
	const unsigned int threads = count.lo & 0xffff;
	const unsigned int cores = count.lo >> 16;

	/* This platform currently initializes a single socket, not QPI peers. */
	if (!cores || threads < cores || threads > 2 * cores || threads > CONFIG_MAX_CPUS)
		die("Grantley: invalid or unsupported CPU thread count\n");
	printk(BIOS_DEBUG, "Grantley: %u cores, %u threads\n", cores, threads);
	return threads;
}

static void get_microcode_info(const void **microcode, size_t *size, int *parallel)
{
	const void *patch = intel_microcode_find();

	/* SIPI assembly writes the patch directly; apply the platform policy here. */
	if (patch && soc_skip_ucode_update(get_current_microcode_rev(), get_microcode_rev(patch)))
		patch = NULL;
	*microcode = patch;
	*size = patch ? get_microcode_size(patch) : 0;
	*parallel = 0;
}

#if CONFIG(HAVE_SMI_HANDLER)
static void grantley_per_cpu_smm_trigger(void)
{
	/* Memory save states share the default SMBASE, so relocation is serial. */
	smm_initiate_relocation();
	/* Recheck microcode after changing SMBASE, retaining the normal policy hook. */
	intel_microcode_load_unlocked(intel_microcode_find());
}
#endif

static const struct mp_ops grantley_mp_ops = {
	.pre_mp_init = grantley_pre_mp_init,
	.get_cpu_count = grantley_get_cpu_count,
	.get_microcode_info = get_microcode_info,
#if CONFIG(HAVE_SMI_HANDLER)
	.get_smm_info = grantley_get_smm_info,
	.pre_mp_smm_init = grantley_smm_initialize,
	.per_cpu_smm_trigger = grantley_per_cpu_smm_trigger,
	.relocation_handler = grantley_smm_relocation_handler,
	.post_mp_init = grantley_smm_lock,
#endif
};

void mp_init_cpus(struct bus *cpu_bus)
{
	if (mp_init_with_smm(cpu_bus, &grantley_mp_ops) != CB_SUCCESS)
		die("Grantley: CPU/SMM initialization failed\n");
}

static struct device_operations grantley_cpu_ops = {
	.init = grantley_core_init,
};

struct device_operations grantley_cpu_bus_ops = {
	.read_resources   = noop_read_resources,
	.set_resources    = noop_set_resources,
	.init             = mp_cpu_bus_init,
#if CONFIG(HAVE_SMI_HANDLER)
	.final            = grantley_cpu_finalize,
#endif
#if CONFIG(HAVE_ACPI_TABLES)
	.acpi_fill_ssdt   = grantley_cpu_fill_ssdt,
#endif
};

static const struct cpu_device_id grantley_cpu_ids[] = {
	{ X86_VENDOR_INTEL, 0x306f0, CPUID_ALL_STEPPINGS_MASK },
	{ X86_VENDOR_INTEL, 0x406f0, CPUID_ALL_STEPPINGS_MASK },
	CPU_TABLE_END
};

static const struct cpu_driver grantley_cpu_driver __cpu_driver = {
	.ops = &grantley_cpu_ops,
	.id_table = grantley_cpu_ids,
};
