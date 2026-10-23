/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpigen.h>
#include <device/device.h>
#include "grantley.h"

void grantley_cpu_fill_ssdt(const struct device *dev)
{
	const unsigned int count = dev_count_cpu();

	/* MADT uses contiguous processor UIDs for enabled CPU devices as well.
	 * No legacy P_BLK or unvalidated server P-state/C-state tables. */
	for (unsigned int i = 0; i < count; i++) {
		acpigen_write_processor(i, 0, 0);
		acpigen_pop_len();
	}
	acpigen_write_processor_package("PPKG", 0, count);
	acpigen_write_processor_cnot(count);
}
