/* SPDX-License-Identifier: GPL-2.0-only */

#include <bootmem.h>
#include <boot/tables.h>
#include <boot/coreboot_tables.h>
#include <symbols.h>
#include <assert.h>
#include <acpi/acpi.h>
#include <cbmem.h>
#include <commonlib/device_tree.h>
#include <console/console.h>
#include <mcall.h>

unsigned long acpi_arch_fill_madt(acpi_madt_t *madt, unsigned long current)
{
	return current;
}

/* Room for the /chosen/coreboot-table property and late board FDT fixups. */
#define FDT_FIXUP_SLACK 1024

/*
 * Reserve the buffer for the FDT that arch_prog_run() hands to the next stage.
 * This has to happen before the coreboot table describes the CBMEM range, so
 * that the payload and OS see the buffer as reserved.
 */
static void reserve_next_fdt(void)
{
	void *fdt = HLS()->fdt;

	if (!fdt || !fdt_is_valid(fdt))
		return;

	/*
	 * Size it the way arch_prog_run() will flatten it: dt_flatten() does not
	 * share property names, so its output can be larger than the input FDT.
	 */
	struct device_tree *tree = fdt_unflatten(fdt);
	if (!tree)
		return;

	size_t size = dt_flat_size(tree) + FDT_FIXUP_SLACK + CONFIG_RISCV_FDT_HEADROOM;
	if (!cbmem_add(CBMEM_ID_FDT, size))
		printk(BIOS_WARNING, "Could not reserve %zu bytes for the next-stage FDT\n",
		       size);
}

void arch_write_tables(uintptr_t coreboot_table)
{
	if (CONFIG(HAVE_ACPI_TABLES))
		acpi_allocate_write_tables();

	reserve_next_fdt();
}

void bootmem_arch_add_ranges(void)
{
	if (CONFIG(RISCV_OPENSBI) && REGION_SIZE(opensbi) > 0)
		bootmem_add_range((uintptr_t)_opensbi, REGION_SIZE(opensbi),
				  BM_MEM_OPENSBI);
}

void lb_arch_add_records(struct lb_header *header)
{
}
