/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpigen.h>
#include <cpu/x86/msr.h>
#include <device/device.h>
#include "grantley.h"

#define MSR_CORE_THREAD_COUNT 0x35

void grantley_cpu_fill_ssdt(const struct device *dev)
{
	const unsigned int count = dev_count_cpu();
	unsigned int cores_per_package = rdmsr(MSR_CORE_THREAD_COUNT).lo & 0xffff;

	if (!cores_per_package)
		cores_per_package = count;

	/* MADT uses contiguous processor UIDs for enabled CPU devices as well. */
	for (unsigned int i = 0; i < count; i++) {
		acpigen_write_processor(i, 0, 0);
		grantley_generate_power_states(i, cores_per_package);
		acpigen_pop_len();
	}
	acpigen_write_processor_package("PPKG", 0, count);
	acpigen_write_processor_cnot(count);
}
