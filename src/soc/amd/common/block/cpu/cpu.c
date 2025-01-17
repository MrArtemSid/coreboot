/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <amdblocks/cpu.h>
#include <cpu/amd/cpuid.h>
#include <cpu/cpu.h>
#include <device/device.h>

int get_cpu_count(void)
{
	return 1 + (cpuid_ecx(0x80000008) & 0xff);
}

unsigned int get_threads_per_core(void)
{
	return 1 + ((cpuid_ebx(CPUID_EBX_CORE_ID) & CPUID_EBX_THREADS_MASK)
		    >> CPUID_EBX_THREADS_SHIFT);
}

struct device_operations amd_cpu_bus_ops = {
	.read_resources	= noop_read_resources,
	.set_resources	= noop_set_resources,
	.init		= mp_cpu_bus_init,
#if CONFIG(HAVE_ACPI_TABLES)
	.acpi_fill_ssdt	= generate_cpu_entries,
#endif
};
