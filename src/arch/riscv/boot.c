/* SPDX-License-Identifier: GPL-2.0-only */

#include <program_loading.h>
#include <vm.h>
#include <arch/boot.h>
#include <arch/encoding.h>
#include <arch/smp/smp.h>
#include <mcall.h>
#include <cbfs.h>
#include <console/console.h>
#include <commonlib/device_tree.h>
#include <cbmem.h>

struct arch_prog_run_args {
	struct prog *prog;
	struct prog *opensbi;
};

/*
 * At the moment devicetree is the default handoff format to the payload instead of
 * coreboot tables (on RISC-V only). That may change in the future, but for now we will put a
 * reference to the coreboot tables inside the FDT (similar to what we do in ACPI).
 */
static void *add_coreboot_table_to_fdt(void *fdt)
{
	if (!fdt || !fdt_is_valid(fdt)) {
		printk(BIOS_ERR, "Invalid FDT provided\n");
		return fdt;
	}

	void *coreboot_table = cbmem_find(CBMEM_ID_CBTABLE);
	if (!coreboot_table) {
		printk(BIOS_ERR, "coreboot table not found in CBMEM\n");
		return fdt;
	}

	struct device_tree *tree = fdt_unflatten(fdt);
	if (!tree) {
		printk(BIOS_ERR, "Failed to unflatten FDT\n");
		return fdt;
	}

	struct device_tree_node *chosen_node = dt_find_node_by_path(tree, "/chosen", NULL, NULL, 1);
	if (!chosen_node) {
		printk(BIOS_ERR, "Failed to find or create /chosen node\n");
		return fdt;
	}

	printk(BIOS_INFO, "Adding cbtable@%p to fdt\n", coreboot_table);
	dt_add_u64_prop(chosen_node, "coreboot-table", (uint64_t)(uintptr_t)coreboot_table);

	/*
	 * OpenSBI grows the FDT in place, so leave headroom after it. The CBMEM
	 * buffer was reserved in arch_write_tables(); fall back to the heap if it
	 * is missing or too small.
	 */
	size_t next_size = dt_flat_size(tree);
	const struct cbmem_entry *entry = cbmem_entry_find(CBMEM_ID_FDT);
	void *next_fdt;

	if (entry && cbmem_entry_size(entry) >= next_size + CONFIG_RISCV_FDT_HEADROOM) {
		next_fdt = cbmem_entry_start(entry);
	} else {
		printk(BIOS_WARNING, "No reserved space for next-stage FDT, using heap\n");
		next_fdt = malloc(next_size);
		if (!next_fdt) {
			printk(BIOS_ERR, "Failed to allocate memory for next-stage FDT\n");
			return fdt;
		}
	}

	dt_flatten(tree, next_fdt);
	return next_fdt;
}

/*
 * A pointer to the Flattened Device Tree passed to coreboot by the boot ROM.
 * Presumably this FDT is also in ROM.
 *
 * This pointer is only used in ramstage!
 */

static void do_arch_prog_run(struct arch_prog_run_args *args)
{
	int hart_id = HLS()->hart_id;
	struct prog *prog = args->prog;
	void *fdt = HLS()->fdt;

	if (prog_cbfs_type(prog) == CBFS_TYPE_FIT_PAYLOAD)
		fdt = prog_entry_arg(prog);

	if (ENV_RAMSTAGE && prog_type(prog) == PROG_PAYLOAD) {
		if (CONFIG(RISCV_OPENSBI)) {
			// tell OpenSBI to switch to Supervisor mode before jumping to payload
			run_payload_opensbi(prog, fdt, args->opensbi, RISCV_PAYLOAD_MODE_S);
		} else {
			run_payload(prog, fdt, RISCV_PAYLOAD_MODE_S);
		}
	} else {
		void (*doit)(int hart_id, void *fdt, void *arg) = prog_entry(prog);
		doit(hart_id, fdt, prog_entry_arg(prog));
	}

	die("Failed to run stage");
}

bool __weak platform_get_preloaded_opensbi(struct prog *sbi)
{
	return false;
}

void arch_prog_run(struct prog *prog)
{
	/*
	 * The other harts read these after smp_resume() has returned, while the
	 * working hart may already run the next stage on the same stack. Keep
	 * them out of the working hart's stack.
	 */
	static struct prog next;
	static struct prog sbi;
	static struct arch_prog_run_args args;

	next = *prog;
	args.prog = &next;
	args.opensbi = NULL;

	if (ENV_RAMSTAGE) {
		int hart_count = CONFIG_MAX_CPUS;
		if (CONFIG(RISCV_GET_HART_COUNT_AT_RUNTIME))
			hart_count = smp_get_hart_count();

		/* Embed coreboot table pointer into fdt, so that payload can find it. */
		void *fdt = HLS()->fdt;
		void *next_fdt = add_coreboot_table_to_fdt(fdt);

		/* Update per hart's fdt with "coreboot-table" node embedded */
		for (int i = 0; i < hart_count; i++) {
			OTHER_HLS(i)->fdt = next_fdt;
		}

		/* In case of OpenSBI we have to load it before resuming all HARTs */
		if (CONFIG(RISCV_OPENSBI)) {
			sbi = (struct prog)PROG_INIT(PROG_OPENSBI, CONFIG_CBFS_PREFIX "/opensbi");

			if (!platform_get_preloaded_opensbi(&sbi) &&
			    !selfload_check(&sbi, BM_MEM_OPENSBI))
				die("OpenSBI load failed");

			args.opensbi = &sbi;
		}
	}

	smp_resume((void (*)(void *))do_arch_prog_run, &args);
}
